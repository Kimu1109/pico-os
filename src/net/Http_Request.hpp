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
//
// **送る本文をRAMに持たずに流すこともできる**(beginStream()。IHttpBodySourceから少しずつ読んで送る)。
// 録音のWAV(数MB)を文字起こしのAPIへ送るためのもの。1回のupdate()で送るのはkSendPerUpdateまでで、
// 送っている間もフレームは回る。送り終える前に相手が応答して閉じた(401等)ときは、その応答を読んで返す。
class IHttpBodySource {
    public:
        virtual ~IHttpBodySource() = default;
        // 本文の全長(Content-Lengthに出す)
        virtual size_t size() const = 0;
        // 続きを buf へ最大 cap バイト入れ、入れた数を返す。0は終わり、負は読めない
        virtual int read(uint8_t* buf, size_t cap) = 0;
        // 最初から読み直す(接続し直して送り直すとき)。できなければfalse
        virtual bool rewind() = 0;
};

class HttpRequest : public Task {
    public:
        enum class Method : uint8_t { GET, POST, PUT, PATCH, Delete };

        static constexpr unsigned long kTimeoutMs = 10000;
        static constexpr unsigned long kConnectTimeoutMs = 3000;
        static constexpr int kMaxRedirects = 3;
        static constexpr unsigned long kLongTransferMaxMs = 30UL * 60UL * 1000UL;
        static constexpr size_t kReadPerUpdate = 1024;
        // beginStream()の本文を1回のupdate()で送る上限
        static constexpr size_t kSendPerUpdate = 8192;
        // 足すヘッダ1行(名前: 値)の上限。送るときの溜め場所(512B)に収まる長さ
        static constexpr size_t kMaxHeaderLineBytes = PICO_STR_512B;

        enum class Fail : uint8_t {
            None,
            ClockNotSet,    // httpsなのに時計が合っていない(証明書の期限を確かめられない)
            TlsFailed,      // 証明書が信頼できない等(詳細はfailureToStr())
            ConnectFailed,
            SendFailed,
            BodyRead,       // beginStream()の本文を読めない(SDのファイルが読めない・短い)
            Timeout,
            TooManyRedirects,
            BadRedirect,
            Response,       // 応答の解釈に失敗、または書き込み先が拒否した(詳細はresponse().error())
        };

        HttpRequest() = default;

        // body/content_typeはnullptr(またはbody_len=0)ならボディ無しのリクエストになる
        // (POST/PUT/PATCHはそのとき`Content-Length: 0`を送る)。
        // body/content_typeの指すメモリの寿命は、呼び出し側がこのリクエストが完了する
        // (getStatus()がPROCESSING以外になる)まで保証すること(IHttpSinkと同じ約束)。
        bool begin(const Url& url, Method method, IHttpSink* sink,
                   const void* body = nullptr, size_t body_len = 0,
                   const char* content_type = nullptr);

        // 本文を source から少しずつ読んで送る(大きなファイル向け)。source の寿命は begin() の body と同じ約束。
        // source->size() が0なら本文の無い要求と同じ
        bool beginStream(const Url& url, Method method, IHttpSink* sink,
                         IHttpBodySource* source, const char* content_type);

        void update() override;
        // 進行中の要求をやめる。持っている接続も閉じる
        void cancel();

        // 接続を使い回すか(既定false = 毎回 Connection: close)
        void setKeepAlive(bool on);
        // 要求ごとに足すヘッダ1行("Authorization: Bearer xxx" のようにCRLF抜きで)。
        // nullptr/空で消す。入りきらなければfalse(消えた状態になる)
        bool setExtraHeader(const char* line);
        // setExtraHeader()の複数行版。"Name: value"を1行ずつ足す(CRLF区切りで溜める)。
        // 名前・値にCR/LFや制御文字があったり、入りきらなければfalse(それまでの分は残る)。
        // 1行(名前: 値)は kMaxHeaderLineBytes 未満、全部で1KiB未満。
        // setExtraHeader()/要求ごとに clearExtraHeaders() で空にする
        bool addExtraHeader(const char* name, const char* value);
        void clearExtraHeaders(){ extra_header_.clear(); }
        // 大きなダウンロード向け: 10秒の全体タイムアウトの代わりに「何も受け取れない時間」が
        // kTimeoutMs続いたときだけ打ち切る(全体はkLongTransferMaxMsまで)。既定はfalse
        void setIdleTimeout(bool on){ idle_timeout_ = on; }
        // 打ち切るまでの時間(既定 kTimeoutMs)。setIdleTimeout(true) なら「何も届かない時間」の上限になる。
        // 送り終えてから相手が考え込む(音声の文字起こし等)ときに延ばす
        void setTimeoutMs(unsigned long ms){ timeout_ms_ = ms > 0 ? ms : kTimeoutMs; }
        // beginStream()の本文を送ったバイト数と全長(進み具合の表示用)
        size_t bodySent() const { return body_sent_; }
        size_t bodyTotal() const { return body_src_ ? body_src_->size() : body_len_; }
        // 本文を送り終えて、応答を待っている/読んでいる
        bool waitingResponse() const { return phase == Phase::Receiving; }
        // GETの3xxを自動で追うか(既定true)。falseなら3xxをそのまま成功として返す
        // (キャプティブポータルの判定のように、転送先そのものを知りたいとき用)
        void setFollowRedirects(bool on){ follow_redirects_ = on; }
        // 使い回すために持っている接続を閉じる(TLSの約40KBを返す)
        void closeConnection();
        // 次のbegin()で使い回せる接続を持っているか
        bool hasIdleConnection() const { return conn_reusable_; }
        // 今の(直前の)要求は持っていた接続を使い回して送ったか
        bool reusedConnection() const { return reused_; }

        const HttpResponse& response() const { return res; }
        const Url& currentUrl() const { return url; }
        Fail failure() const { return fail_; }
        const char* failureToStr() const;

        static const char* MethodToStr(Method m);

    private:
        enum class Phase : uint8_t { Idle, Connecting, Sending, SendingBody, Receiving, Ended };

        HttpTransport client;
        HttpResponse res;
        IHttpSink* sink_ = nullptr;

        Url url;
        Method method_ = Method::GET;
        const void* body_ = nullptr;
        size_t body_len_ = 0;
        IHttpBodySource* body_src_ = nullptr;
        size_t body_sent_ = 0;
        // multipart の "multipart/form-data; boundary=…"(約60B)が入る大きさ
        FixedString<PICO_STR_L> content_type_;

        // ---- keep-alive ----
        bool keep_alive_ = false;
        FixedString<PICO_STR_1KiB> extra_header_; //複数行(CRLF区切り、末尾のCRLFは無し。合計1KiB未満、1行は512B未満)
        bool idle_timeout_ = false;
        unsigned long timeout_ms_ = kTimeoutMs;
        bool follow_redirects_ = true;
        unsigned long last_activity_ms = 0;
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
        // beginStream()の本文の続きを送る。送り終えたら Receiving へ進む
        void sendBodyChunk();
        // begin()/beginStream()の共通の下準備。持っていた接続を使い回すならtrue
        bool beginCommon(const Url& url, Method method, IHttpSink* sink, const char* content_type);
        void finishWith(TaskTools::Status s, Fail f);
        bool followRedirect();
};
