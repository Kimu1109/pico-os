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
//
// **keep-alive(接続の使い回し)に対応する(既定は無効)。** setKeepAlive(true)にすると、
// 応答の後も接続を閉じずに持っておき、次のbegin()が同じ相手(ホスト・ポート・http/https)なら
// 接続とTLSのハンドシェイクを飛ばしてすぐ送る。HTTPSはハンドシェイクで1〜2秒画面が止まるので、
// 数秒ごとに問い合わせるチャットのような使い方ではこれが無いと固まり続ける。
//   - 持っている間はTLSの道具一式(約40KB)も持ち続ける。要らなくなったらcloseConnection()
//   - 相手が黙って閉じていた場合(アイドルのタイムアウト等)は、何も受け取れなかった時点で
//     1回だけ繋ぎ直して送り直す。**POSTも送り直す**ので、相手が処理した直後に閉じた
//     ごくまれな場合は二重に届き得る(チャット程度なら許容する割り切り)
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
        // 進行中の要求をやめる。持っている接続も閉じる
        void cancel();

        // 接続を使い回すか(既定false = 毎回 Connection: close)
        void setKeepAlive(bool on);
        // 要求ごとに足すヘッダ1行("Authorization: Bearer xxx" のようにCRLF抜きで)。
        // nullptr/空で消す。入りきらなければfalse(消えた状態になる)
        bool setExtraHeader(const char* line);
        // 使い回すために持っている接続を閉じる(TLSの約40KBを返す)
        void closeConnection();
        // 次のbegin()で使い回せる接続を持っているか
        bool hasIdleConnection() const { return conn_reusable_; }

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

        // ---- keep-alive ----
        bool keep_alive_ = false;
        FixedString<PICO_STR_LL> extra_header_;
        bool conn_reusable_ = false; // 前の応答の後、接続を開けたまま持っている
        Url conn_url_;               // その接続の相手(ホスト・ポート・schemeだけを見る)
        bool reused_ = false;        // 今の要求は持っていた接続で送った
        bool got_bytes_ = false;     // 今の要求で1バイトでも受け取った

        Phase phase = Phase::Idle;
        Fail fail_ = Fail::None;
        int redirects = 0;
        unsigned long started_ms = 0;

        bool startRequest(bool reuse = false);
        static bool SameEndpoint(const Url& a, const Url& b);
        // 使い回した接続が死んでいたとき、1回だけ繋ぎ直して送り直す
        bool retryOnFreshConnection();
        bool sendRequestLine();
        void finishWith(TaskTools::Status s, Fail f);
        bool followRedirect();
};
