#pragma once

#include <SdFat.h>

#include "apps/store/App_Repo.hpp"
#include "net/Http_Request.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// アプリストアの通信係とインストール係(仕様は APP_REPO.md)。画面(StoreScene)とは分けてあり、
// ホストテスト(run_net.sh の app_store_net_test)が本物の通信で一通り動かす。
//
// 流れ:
//   beginRefresh() … catalog.tsv → catalog.sig を取り、署名と版の番号(serial)を確かめてから
//                    /sys/store/catalog.tsv へ差し替える。確かめられなければ手元の一覧はそのまま
//   beginInstall() … files.tsv(カタログのSHA-256と照合)→ 各ファイル(files.tsvのSHA-256と照合)を
//                    /sys/store/staging/ へ取り、app.cfg の id・版・権限を確かめてから
//                    /lua/apps/<dir>/ へ1つずつ移し、登録簿へ入れる
//   remove()       … /lua/apps/<dir>/ を消して登録簿から外す(同期)
//
// 通信は HttpRequest 1本を keep-alive で使い回す(HTTPSの1〜2秒のハンドシェイクを1回にするため)。
// 持っている間はTLSの約40KBを持つので、画面を離れるときに close() すること。
// このクラス自体は約13KB(カタログ40件)なので、画面が開いている間だけ new する。
class AppStore {
    public:
        enum class State : uint8_t { Idle, Busy, Done, Failed };
        enum class Installed : uint8_t {
            No,       // 入っていない
            Same,     // 同じ版が入っている
            Update,   // 古い版が入っている
            Newer,    // カタログより新しい版が入っている(手元で書き換えた等)
            Foreign,  // 同じ名前のディレクトリに、ストアのものでない(idが無い)アプリがある
            Conflict, // 同じ名前のディレクトリに、別のidのアプリがある(入れられない)
        };

        static constexpr const char* kDefaultUrl = "https://kimu1109.github.io/pico-os/apps/";
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
        bool busy() const { return state_ == State::Busy; }
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

        enum class Step : uint8_t { None, Catalog, Signature, Files, File };

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
        int count_ = 0;
        uint32_t serial_ = 0;
        uint32_t revision_ = 0;

        // catalog.tsv / files.tsv を1行ずつ読む作業場所(スタックに512バイトを置かないため)
        char line_[AppRepo::kMaxLineBytes + 2];
        char aux_[AppRepo::kMaxLineBytes + 2];

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
        void onFile();
        bool startNextFile();
        bool checkStagedConfig();
        bool commitInstall();

        // カタログのファイル(catalog.tsv)を読み直して entries_ へ。serialを返す
        bool parseCatalogFile(const char* path, uint32_t& serial, bool fill);
        // files.tsv の n 番目(0始まり)の行を読む
        bool readFileLine(const char* path, int n, AppRepo::FileEntry& out);
        // files.tsv の一覧に rel(大小を区別しない)があるか
        bool listHas(const char* list_path, const char* rel);
        void refreshInstalled();
};
