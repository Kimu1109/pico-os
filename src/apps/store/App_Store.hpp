#pragma once

#include <SdFat.h>

#include "apps/store/App_Repo.hpp"
#include "apps/store/App_Install.hpp"
#include "net/Http_Request.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// アプリストアの通信係とインストール係(仕様は docs/APP_REPO.md)。画面(StoreScene)とは分けてあり、
// ホストテスト(run_net.sh の app_store_net_test)が本物の通信で一通り動かす。
//
// 流れ:
//   beginRefresh() … catalog.tsv → catalog.sig を取り、署名と版の番号(serial)を確かめてから
//                    /sys/store/catalog.tsv へ差し替える。確かめられなければ手元の一覧はそのまま
//   beginInstall() … files.tsv(カタログのSHA-256と照合)→ files.sig(作者の鍵の署名)→
//                    各ファイル(files.tsvのSHA-256と照合)を /sys/store/staging/ へ取り、
//                    app.cfg の id・版・権限を確かめてから /lua/apps/<dir>/ へ1つずつ移し、登録簿へ入れる。
//                    初めて入れたときの作者の鍵を覚え(/sys/store/installed/<id>.key)、更新では同じ鍵だけを受け付ける
//   remove()       … /lua/apps/<dir>/ を消して登録簿から外す(同期)
//
// 通信は HttpRequest 1本を keep-alive で使い回す(HTTPSの1〜2秒のハンドシェイクを1回にするため)。
// 持っている間はTLSの約40KBを持つので、画面を離れるときに close() すること。
// このクラス自体は約27KB(カタログ40件で約20KB。ホストで測った値)なので、画面が開いている間だけ new する。
class AppStore {
    public:
        enum class State : uint8_t { Idle, Busy, Done, Failed };
        using Installed = AppInstall::Installed;

        static constexpr const char* kAppName = "ストア";  // 登録簿の名前(StoreScene::kAppName。通知から開く)
        static constexpr const char* kDefaultUrl = "https://kimu1109.github.io/pico-os-apps/";  // 投稿用リポジトリ kimu1109/pico-os-apps の Pages
        static constexpr int kMaxExtraKeys = 4;

        AppStore();
        ~AppStore();

        // /sys/store.cfg(url = / key = 公開鍵の16進。どちらも無くてよい)を読む
        void loadConfig();
        // テスト用の差し替え口(loadConfig()の後で呼ぶ)
        bool setBaseUrl(const char* url);
        bool addTrustedKey(const uint8_t pub[32]);
        const char* baseUrl() const { return base_url_.c_str(); }
        // HTTPSの相手を名前で確かめない/追加のCAを使う等は HttpTransport の設定に従う

        // 前回確かめて保存したカタログを読む(同じURLのものだけ)。無ければfalse
        bool loadCachedCatalog();

        bool beginRefresh();
        // allow_replace … Installed::Foreign のディレクトリへ上書きしてよいか(利用者に確かめた後)
        bool beginInstall(int index, bool allow_replace = false);
        bool remove(int index);
        void update();
        void cancel();
        // 使い回すために持っている接続を閉じる(TLSの約40KBを返す)
        void close();

        State state() const { return state_; }
        // 通信中か、まとめて更新の途中(1つ入れ終えて次を始める前の1回も含む)
        bool busy() const { return state_ == State::Busy || batch_current_ >= 0; }
        // 結果・進み具合の1行。日本語でそのまま画面へ出せる
        const char* message() const { return message_.c_str(); }
        // 取得中の合計とそのうち済んだバイト数(インストール中だけ意味を持つ)
        uint32_t progressDone() const { return progress_done_ + (step_ == Step::File ? file_sink_.bytes() : 0); }
        uint32_t progressTotal() const { return progress_total_; }
        // カタログ・入っているかどうかが変わるたびに進む(画面の作り直しに使う)
        uint32_t revision() const { return revision_; }

        int count() const { return count_; }
        const AppRepo::Entry* entry(int index) const;
        Installed installed(int index) const;
        // 入っている版(入っていなければ空)
        const char* installedVersion(int index) const;
        // 新しく入れられるか(取り下げ・危険の印が無く、本体の版の条件 min_os を満たす)
        bool installable(int index) const;
        // 本体が古くて入れられない(min_os)
        bool needsNewerOs(int index) const;
        // 一覧の版を見送っている(前の版へ戻したときの版。まとめて更新・更新の知らせから外す)
        bool held(int index) const;
        // 更新で権限が増える(まとめて更新には入れず、1つずつ確かめてもらう)
        bool permsGrow(int index) const;
        // まとめて更新に入れられるか: 古い版が入っていて、入れられて、見送っておらず、権限が増えない
        bool batchUpdatable(int index) const;
        int batchCount() const;
        // 更新のある(見送っておらず、本体の条件も満たす)アプリの数。権限が増えるものも数える(更新の知らせ用)
        int updateCount() const;
        // batchUpdatable() なものを順に全部入れる(1つが失敗しても続ける)
        bool beginUpdateAll();
        bool batchRunning() const { return batch_mask_ != 0 || batch_current_ >= 0; }

        // 前の版(更新の前に残したもの)。無ければfalse
        bool previousVersion(int index, FixedString<PICO_STR_S>& out) const;
        // 前の版へ戻す(同期)
        bool rollback(int index);
        // 危険の印(blocked)が付いたアプリが入っているか
        bool blockedInstalled(int index) const;
        // 入っているアプリを開く(Installed::No/Conflict以外)
        bool launch(int index);
        // インストール先(/lua/apps/<dir>)
        bool appDir(int index, FixedString<PICO_PATH_LEN>& out) const;

        // ---- テスト・診断用 ----
        uint32_t catalogSerial() const { return serial_; }

    private:
        // 受け取ったものをSDへ書きながらSHA-256を取る
        class FileSink : public IHttpSink {
            public:
                bool open(const char* path, uint32_t limit);
                void close();
                bool write(const void* data, size_t len) override;
                uint32_t bytes() const { return bytes_; }
                void digest(uint8_t out[32]){ sha_.finish(out); }
                bool overflowed() const { return overflow_; }
            private:
                FsFile file_;
                Sha256 sha_;
                uint32_t bytes_ = 0;
                uint32_t limit_ = 0;
                bool overflow_ = false;
        };
        // 小さなもの(catalog.sig)をメモリへ
        class MemSink : public IHttpSink {
            public:
                void reset(){ len_ = 0; overflow_ = false; buf_[0] = '\0'; }
                bool write(const void* data, size_t len) override;
                const char* text() const { return buf_; }
                bool overflowed() const { return overflow_; }
            private:
                char buf_[AppRepo::kMaxSigBytes + 1] = {};
                size_t len_ = 0;
                bool overflow_ = false;
        };

        enum class Step : uint8_t { None, Catalog, Signature, Files, FilesSig, File };

        HttpRequest http_;
        FileSink file_sink_;
        MemSink mem_sink_;
        HttpBodyGate gate_;
        Step step_ = Step::None;
        State state_ = State::Idle;
        FixedString<PICO_STR_LL> message_;

        FixedString<PICO_STR_L> base_url_;
        uint8_t keys_[kMaxExtraKeys][32] = {};
        int key_count_ = 0;

        AppRepo::Entry entries_[AppRepo::kMaxEntries];
        Installed installed_[AppRepo::kMaxEntries] = {};
        FixedString<PICO_STR_S> installed_ver_[AppRepo::kMaxEntries];
        uint8_t installed_perms_[AppRepo::kMaxEntries] = {};
        uint64_t held_mask_ = 0;   // 見送っている版(held())の印。kMaxEntries <= 64
        int count_ = 0;
        uint32_t serial_ = 0;
        uint32_t revision_ = 0;

        // catalog.tsv / files.tsv を1行ずつ読む作業場所(スタックに512バイトを置かないため)
        AppInstall::Work work_;

        // ---- まとめて更新 ----
        static_assert(AppRepo::kMaxEntries <= 64, "まとめて更新の印は64件まで");
        uint64_t batch_mask_ = 0;  // これから入れるもの(番号のビット)
        int batch_current_ = -1;   // いま入れているもの
        int batch_ok_ = 0;
        int batch_failed_ = 0;
        FixedString<PICO_STR_LL> batch_first_error_;
        void advanceBatch();

        // ---- インストール中 ----
        int target_ = -1;
        bool allow_replace_ = false;
        int file_index_ = 0;
        int file_count_ = 0;
        AppRepo::FileEntry file_;
        uint32_t progress_done_ = 0;
        uint32_t progress_total_ = 0;

        void fail(const char* msg);
        void finishOk(const char* msg);
        bool startGet(const char* rel_url);
        bool trustedKey(const uint8_t pub[32]) const;

        void onCatalog();
        void onSignature();
        void onFiles();
        void onFilesSig();
        void onFile();
        bool startNextFile();

        // カタログのファイル(catalog.tsv)を読み直して entries_ へ。serialを返す
        bool parseCatalogFile(const char* path, uint32_t& serial, bool fill);
        void refreshInstalled();
};
