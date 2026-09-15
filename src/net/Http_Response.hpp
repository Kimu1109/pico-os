#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// 受け取った本文の行き先。
// 文書ならDoc_Cache::Writerへ、検索やdiscoveryならTSVの行パーサへ、と差し替える。
// std::functionにしないのは、キャプチャのたびにヒープを叩くのを避けるため。
class IHttpSink {
    public:
        virtual ~IHttpSink() = default;

        // 受信したぶんを渡す。falseを返すと以降の受信は打ち切られる
        virtual bool write(const void* data, size_t len) = 0;
};

namespace HttpTools {

    enum class Error : uint8_t {
        None,
        BadStatusLine,   // ステータス行が "HTTP/x.y NNN" の形ではない
        LineTooLong,     // ヘッダ1行が長すぎる
        Chunked,         // chunked転送。PROTOCOL.mdで禁止しているので対応しない
        Truncated,       // Content-Lengthに満たないまま接続が切れた
        SinkFailed,      // 書き込み先が受け取りを拒否した(上限超過など)
    };

    inline const char* ErrorToStr(Error e){
        switch(e){
            case Error::None:          return "なし";
            case Error::BadStatusLine: return "ステータス行が不正";
            case Error::LineTooLong:   return "ヘッダ行が長すぎる";
            case Error::Chunked:       return "chunked転送は未対応";
            case Error::Truncated:     return "本文が途中で切れた";
            case Error::SinkFailed:    return "書き込みに失敗";
            default:                   return "不明";
        }
    }
}

// HTTPレスポンスを少しずつ食わせて解釈する。
//
// **ソケットを持たない**のが要点。受信したバイト列をfeed()へ渡すだけなので、
// ネットワーク無しでホストテストから全経路を検証できる(script/host_test/http_test.cpp)。
//
// PROTOCOL.mdの取り決めに合わせてあるところ:
//   - chunked転送は解釈せず、検出したらエラーにする(黙って壊れた本文を書かないため)
//   - Content-Lengthがあればその長さで終端、無ければ接続が閉じるまでを本文とする
//   - 見るヘッダは Content-Length / ETag / Last-Modified / Location / Transfer-Encoding だけ
class HttpResponse {
    public:
        // ヘッダ1行の上限。これを超える行を送ってくるサーバは相手にしない
        static constexpr int kMaxLineLen = 256;

        void reset(IHttpSink* sink);

        // 受信したぶんを食わせる。解釈を続けられる間はtrue
        bool feed(const void* data, size_t len);

        // 接続が閉じられたことを伝える。Content-Length未指定ならここで本文が確定する
        bool finish();

        int statusCode() const { return status_code; }
        bool headersDone() const { return state >= State::Body; }
        bool isDone() const { return state == State::Done; }
        bool hasFailed() const { return state == State::Failed; }
        HttpTools::Error error() const { return err; }

        // 条件付きGETで送り返す値。ETagを優先し、無ければLast-Modifiedが入る
        const FixedString<PICO_STR_M>& validator() const { return validator_; }
        // 3xxのときの転送先
        const FixedString<PICO_STR_L>& location() const { return location_; }

        int32_t contentLength() const { return content_length; }
        uint32_t bodyBytes() const { return body_bytes; }

        // 本文を伴わない応答(304や、3xxで転送先だけ見たい場合)
        bool isNotModified() const { return status_code == 304; }
        bool isRedirect() const {
            return status_code == 301 || status_code == 302
                || status_code == 307 || status_code == 308;
        }

    private:
        enum class State : uint8_t { Status = 0, Headers = 1, Body = 2, Done = 3, Failed = 4 };

        State state = State::Status;
        HttpTools::Error err = HttpTools::Error::None;

        IHttpSink* sink = nullptr;

        char line[kMaxLineLen];
        int line_len = 0;
        bool line_overflow = false;

        int status_code = 0;
        int32_t content_length = -1;  // -1 = 未指定(接続が閉じるまでが本文)
        uint32_t body_bytes = 0;

        FixedString<PICO_STR_M> validator_;
        FixedString<PICO_STR_L> location_;
        bool has_etag = false;

        bool fail(HttpTools::Error e);
        friend class HttpBodyGate;
        bool pushLineChar(char c);
        bool handleStatusLine();
        bool handleHeaderLine();
        bool consumeBody(const uint8_t* data, size_t len, size_t& consumed);
};


// 200以外の本文を書き込み先へ流さないための中継。
// 3xxの案内文や404のエラーページがキャッシュへ入るのを防ぐ。
//
// **判定は「書き込みの瞬間」に行うこと。**
// ヘッダの終わりと本文の先頭が同じ受信で届くことがあり、
// 「feed()が終わってからゲートを開ける」やり方だと、その回に含まれていた
// 本文の先頭を取りこぼす。実際にサーバ情報の先頭36バイトが欠けた
// (小さいファイルほどヘッダと一緒に届くので当たりやすい)。
// write()が呼ばれる時点ではヘッダの解釈は必ず終わっているので、
// ここでstatusCode()を見れば取りこぼしようがない。
class HttpBodyGate : public IHttpSink {
    public:
        void attach(const HttpResponse* res, IHttpSink* inner){
            res_ = res;
            inner_ = inner;
        }
        void detach(){
            res_ = nullptr;
            inner_ = nullptr;
        }

        bool write(const void* data, size_t len) override {
            //捨てるだけで、受信そのものは続ける(接続を最後まで読み切る)
            if(!res_ || res_->statusCode() != 200) return true;
            return inner_ ? inner_->write(data, len) : true;
        }

    private:
        const HttpResponse* res_ = nullptr;
        IHttpSink* inner_ = nullptr;
};
