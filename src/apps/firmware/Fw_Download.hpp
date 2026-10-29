#pragma once

#include <SdFat.h>

#include "apps/firmware/Fw_Index.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "net/Http_Request.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// Webからファームの更新ファイルを取る係(docs/FIRMWARE_UPDATE.md「Webから取る」)。
//
//   beginIndex() … <置き場>/index.tsv を取り、今の本体に合う版(構成が同じ・署名の決まり)の一覧にする
//   beginFile(i) … 一覧の i 番目の .pfw を SD の <dir>/<ファイル名>.part へ書きながら SHA-256 を取り、
//                   一覧の大きさ・SHA-256 と合えば <dir>/<ファイル名> へ名前を変える
//
// 書き込み(見出し・署名・確認・内蔵フラッシュへ)はここではしない。落としたファイルを
// いつものSDからの更新(FirmwareFunctions::StartFromSd)へ渡す。確認のダイアログで止まっている間に
// 通信が切られることが無く、電池・署名・置き場の検査をそのまま使えるため。
//
// 通信は HttpRequest 1本を keep-alive で使い回す(一覧とファイルが同じ相手なら、HTTPSのハンドシェイクは1回)。
// 持っている間はTLSの約40KBを持つので、書き込みに移る前に close() すること。
// このクラスは一覧の16件ぶん(約4KB)と受け取りの溜め場所(8KB)を持つので、画面が開いている間だけ new する。
// OSへのつなぎ(Wi-Fi・設定ファイル・画面)は services/Firmware_Functions。
class FwDownload {
    public:
        enum class State : uint8_t {
            Idle,
            Index,      // 一覧を取っている
            Listed,     // 一覧を取れた(entries() から選ぶ)
            File,       // 更新ファイルを取っている
            Done,       // SDに置けた(path())
            Failed,
        };

        // CIが置く GitHub Pages(.github/workflows/web-pages.yml)
        static constexpr const char* kDefaultUrl = "https://kimu1109.github.io/pico-os/firmware/";
        // 1回の update() で回す HttpRequest::update() の回数と時間の上限
        static constexpr int kPumpPerUpdate = 16;
        static constexpr unsigned long kPumpBudgetMs = 10;

        FwDownload();
        ~FwDownload();

        // 置き場のURL("https://.../firmware/"。末尾の/が無ければ足す)。長すぎる・http(s)でなければfalse
        bool setBaseUrl(const char* url);
        const char* baseUrl() const { return base_url_.c_str(); }

        bool beginIndex(const FwIndex::Filter& filter);
        // dir = 置くディレクトリ("/firmware"。無ければ作る)
        bool beginFile(int index, const char* dir);
        void update();
        void cancel();
        // 使い回すために持っている接続を閉じる(TLSの約40KBを返す)
        void close();

        State state() const { return state_; }
        bool busy() const { return state_ == State::Index || state_ == State::File; }
        const char* message() const { return message_.c_str(); }

        int count() const { return count_; }
        const FwIndex::Entry* entry(int index) const;
        const FwIndex::Result& indexResult() const { return result_; }
        // 置いたファイル(Done のとき)
        const char* path() const { return path_.c_str(); }

        uint32_t progressDone() const { return state_ == State::File ? file_sink_.bytes() : 0; }
        uint32_t progressTotal() const { return state_ == State::File && target_ >= 0 ? entries_[target_].bytes : 0; }

    private:
        class MemSink : public IHttpSink {
            public:
                void reset(){ len_ = 0; overflow_ = false; }
                bool write(const void* data, size_t len) override;
                const char* data() const { return buf_; }
                size_t size() const { return len_; }
                bool overflowed() const { return overflow_; }
            private:
                char buf_[FwIndex::kMaxBytes];
                size_t len_ = 0;
                bool overflow_ = false;
        };
        class FileSink : public IHttpSink {
            public:
                bool open(const char* path, uint32_t limit);
                void close();
                bool write(const void* data, size_t len) override;
                uint32_t bytes() const { return bytes_; }
                void digest(uint8_t out[32]){ sha_.finish(out); }
                bool overflowed() const { return overflow_; }
                bool failed() const { return failed_; }
            private:
                FsFile file_;
                Sha256 sha_;
                uint32_t bytes_ = 0;
                uint32_t limit_ = 0;
                bool overflow_ = false;
                bool failed_ = false;
        };

        bool startGet(const char* name, IHttpSink* sink);
        void fail(const char* text);
        void onIndex();
        void onFile();

        HttpRequest http_;
        HttpBodyGate gate_;
        MemSink mem_sink_;
        FileSink file_sink_;
        State state_ = State::Idle;
        FixedString<PICO_STR_LL> message_;
        FixedString<PICO_STR_L> base_url_;
        FwIndex::Filter filter_{};
        FixedString<PICO_STR_M> filter_env_;
        FwIndex::Entry entries_[FwIndex::kMaxEntries];
        FwIndex::Result result_{};
        int count_ = 0;
        int target_ = -1;
        FixedString<PICO_PATH_LEN> path_;
        FixedString<PICO_PATH_LEN> part_path_;
};
