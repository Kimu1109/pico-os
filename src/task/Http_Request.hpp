#pragma once

#include "task/Task.hpp"
#include "net/Http_Response.hpp"
#include "net/Http_Transport.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"

// メソッド・任意の送信ボディを指定できる汎用の非ブロッキングHTTPリクエスト。
//
// `Http_Get`(GET専用。条件付きGET・キャッシュ用の304判定・200以外の本文を
// 捨てるHttpBodyGateを内蔵)とは別クラスにしてある。Luaの`pico.http_request()`
// のような「サーバへ何を送ってどう返ってくるか丸ごとスクリプトに見せたい」
// 用途では、Http_Getの以下2点がそのまま使えないため:
//   - HttpBodyGateがstatusCode()==200以外の本文を黙って捨てる
//     (Markdownブラウザのキャッシュには正しい判断だが、201/400等の本文を
//     読みたい汎用APIには不適)
//   - 200/304以外を一律FAILED扱いにする(呼び出し側がstatusCodeで判断する
//     という発想自体はここでも同じだが、こちらはその判断のためにこそ
//     本文を渡す必要がある)
// 接続/送信/受信ループ・タイムアウト・リダイレクト追跡の骨格はHttp_Getと同じ
// (実績のある形をそのまま踏襲。飛び先はGETのときだけ自動で追う。POST等は
// ボディを送り直すかの判断が要る(RFC的にも一律ではない)ため、3xxが返っても
// そのまま呼び出し側へ渡す)。
//
// 本文の行き先はHttp_Getと同じくIHttpSink*(非所有。寿命は呼び出し側が保証)。
// ゲートを挟まないので、statusCodeによらずsink->write()が呼ばれる。
class HttpRequest : public Task {
    public:
        enum class Method : uint8_t { GET, POST, PUT, PATCH, Delete };

        static constexpr unsigned long kTimeoutMs = 10000;
        static constexpr unsigned long kConnectTimeoutMs = 3000;
        static constexpr int kMaxRedirects = 3;
        static constexpr size_t kReadPerUpdate = 1024;

        enum class Fail : uint8_t {
            None,
            ClockNotSet,    // httpsなのに時計が合っていない(証明書の期限を確かめられない)
            TlsFailed,      // 証明書が信頼できない等(詳細はfailureToStr())
            ConnectFailed,
            SendFailed,
            Timeout,
            TooManyRedirects,
            BadRedirect,
            Response,       // 応答の解釈に失敗、または書き込み先が拒否した(詳細はresponse().error())
        };

        HttpRequest() = default;

        // body/content_typeはnullptr(またはbody_len=0)ならボディ無しのリクエストになる。
        // body/content_typeの指すメモリの寿命は、呼び出し側がこのリクエストが完了する
        // (getStatus()がPROCESSING以外になる)まで保証すること(IHttpSinkと同じ約束)。
        bool begin(const Url& url, Method method, IHttpSink* sink,
                   const void* body = nullptr, size_t body_len = 0,
                   const char* content_type = nullptr);

        void update() override;
        void cancel();

        const HttpResponse& response() const { return res; }
        const Url& currentUrl() const { return url; }
        Fail failure() const { return fail_; }
        const char* failureToStr() const;

        static const char* MethodToStr(Method m);

    private:
        enum class Phase : uint8_t { Idle, Connecting, Sending, Receiving, Ended };

        HttpTransport client;
        HttpResponse res;
        IHttpSink* sink_ = nullptr;

        Url url;
        Method method_ = Method::GET;
        const void* body_ = nullptr;
        size_t body_len_ = 0;
        FixedString<PICO_STR_M> content_type_;

        Phase phase = Phase::Idle;
        Fail fail_ = Fail::None;
        int redirects = 0;
        unsigned long started_ms = 0;

        bool startRequest();
        bool sendRequestLine();
        void finishWith(TaskTools::Status s, Fail f);
        bool followRedirect();
};
