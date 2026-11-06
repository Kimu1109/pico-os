#pragma once

#include <SdFat.h>

#include "apps/ssh/Ssh_Sha256.hpp"
#include "net/Http_Request.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// SDの追加の漢字フォント(/sys/fonts/jpgothic16.pkf・jpgothic24.pkf)をWebから取って置く係。
// docs/design/performance.md「漢字の一部をSDへ」。
//
// 配布元は GitHub Pages の /fonts/(.github/workflows/web-pages.yml が pc/sdcard/sys/fonts/ から写す)。
// 名前に印が入っている(jpgothic16-<印>.pkf)ので、ファームの版が変わってフォントを作り直しても古いものと混ざらない。
// 落とすものの大きさとSHA-256はファームに入っている(src/gui/fonts/JpFont_Data.hpp)ので、一覧のファイルは取らない。
// <置き場>.part へ書きながらSHA-256を取り、大きさ・SHA-256が合ったら名前を変えて、JpFontExt::Reload() する。
// 置き場は /sys/firmware.cfg の font-url で変えられる(PCビルドで手元のサーバから試す用)。
//
// 通信は HttpRequest 1本を keep-alive で使い回す(2つのファイルが同じ相手なので、HTTPSのハンドシェイクは1回)。
// TLSの約40KBを持つので、設定の画面が取得の間だけ new する。
class FontDownload {
    public:
        enum class State : uint8_t { Idle, Running, Done, Failed };

        static constexpr const char* kDefaultUrl = "https://kimu1109.github.io/pico-os/fonts/";
        static constexpr int kPumpPerUpdate = 16;
        static constexpr unsigned long kPumpBudgetMs = 10;

        FontDownload();
        ~FontDownload();

        // 置き場のURL(末尾の/が無ければ足す)。長すぎる・http(s)でなければfalse
        bool setBaseUrl(const char* url);
        const char* baseUrl() const { return base_url_.c_str(); }

        // まだ使えない(SDに無い・合わない)ものを取り始める。Wi-Fi・SDが使えなければ false(message())
        bool begin();
        void update();
        void cancel();

        State state() const { return state_; }
        bool busy() const { return state_ == State::Running; }
        const char* message() const { return message_.c_str(); }
        uint32_t progressDone() const;
        uint32_t progressTotal() const;

    private:
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

        bool startNext();
        void fail(const char* text);
        void onFile();

        HttpRequest http_;
        HttpBodyGate gate_;
        FileSink sink_;
        State state_ = State::Idle;
        FixedString<PICO_STR_LL> message_;
        FixedString<PICO_STR_L> base_url_;
        int pending_[2] = {};   // 取る字の高さ(16/24)
        int pending_count_ = 0;
        int current_ = -1;      // pending_ の何番目を取っているか
        uint32_t done_bytes_ = 0;  // 取り終えたぶん(進み具合の表示)
        FixedString<PICO_PATH_LEN> part_path_;
};
