#pragma once

#include "task/Task.hpp"
#include "net/Http_Response.hpp"
#include "net/Http_Transport.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"

// 1本のHTTP GETを非ブロッキングで回すタスク。
//
// `loop()`は単純なポーリングなので、受信待ちでブロックすると画面ごと固まる。
// NetworkScanと同じく「開始してからupdate()で進捗を見る」形にしてある。
//
// PROTOCOL.mdの取り決めのうち、ここが担当するもの:
//   - http/https のどちらでも繋ぐ(接続の仕方は net/Http_Transport が決める)
//   - `Connection: close` を送る。`Accept-Encoding` は送らない(圧縮させない)
//   - リダイレクト(301/302/307/308)を最大3回まで追う
//   - 10秒で打ち切る
//   - 条件付きGET(手元の検証子を送り、304ならキャッシュを使う)
//
// **接続(connect)だけは同期的**な点に注意。arduino-picoのWiFiClient::connect()が
// 戻るまで待つため、到達しない相手を指すと最大で接続タイムアウトぶん画面が止まる。
// httpsではTLSのハンドシェイクもここに含まれる(Http_Transport.hpp参照)。
// 受信は全てポーリングなので、繋がってしまえば以降フレームを止めない。
// (非同期接続にするにはlwIPを直に叩く必要があり、それは別の段の仕事)
class HttpGet : public Task {
    public:
        // PROTOCOL.md「制限値の一覧」より
        static constexpr unsigned long kTimeoutMs = 10000;
        static constexpr int kMaxRedirects = 3;

        // 接続に使う待ち時間。到達しない相手でここまで画面が止まりうる
        static constexpr unsigned long kConnectTimeoutMs = 3000;

        // 1回のupdate()で読み進めるバイト数の上限。
        // 大きくすると速いがフレームが伸びる
        static constexpr size_t kReadPerUpdate = 1024;

        enum class Fail : uint8_t {
            None,
            ConnectFailed,  // 繋がらない
            ClockNotSet,    // httpsなのに時計が合っていない(証明書の期限を確かめられない)
            TlsFailed,      // 証明書が信頼できない等(詳細はfailureToStr())
            SendFailed,     // リクエストを送れない
            Timeout,
            TooManyRedirects,
            BadRedirect,    // Locationが無い/解決できない
            Response,       // 応答の解釈に失敗(詳細はresponse().error())
        };

        HttpGet() = default;

        // 取得を開始する。sinkの寿命は呼び出し側が保証すること。
        // validatorが非空なら条件付きGETになり、変更が無ければ304が返る
        bool begin(const Url& url, IHttpSink* sink, const char* validator = nullptr);

        void update() override;

        // 途中でやめる
        void cancel();

        const HttpResponse& response() const { return res; }
        const Url& currentUrl() const { return url; }
        Fail failure() const { return fail_; }
        const char* failureToStr() const;

        // 304が返った(手元のキャッシュがそのまま使える)
        bool isNotModified() const { return res.isNotModified(); }

    private:
        enum class Phase : uint8_t { Idle, Connecting, Sending, Receiving, Ended };

        HttpTransport client;
        HttpResponse res;
        //3xx/4xxの本文をシンクへ流さないための中継(判定は書き込みの瞬間に行う)
        HttpBodyGate gate;
        IHttpSink* sink_ = nullptr;

        Url url;
        FixedString<PICO_STR_M> validator;

        Phase phase = Phase::Idle;
        Fail fail_ = Fail::None;
        int redirects = 0;
        unsigned long started_ms = 0;

        bool startRequest();          // 接続〜送信までを仕掛け直す
        bool sendRequestLine();
        void finishWith(TaskTools::Status s, Fail f);
        bool followRedirect();
};
