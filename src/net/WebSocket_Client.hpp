#pragma once

#include "net/Http_Transport.hpp"
#include "net/Ws_Frame.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// WebSocket(RFC 6455)のクライアント。ws:// は素のTCP、wss:// はTLS(net/Http_Transport)。
// Home Assistant の WebSocket API・リアルタイムのチャット等、サーバから即座に届く通知を受けるのに使う。
//
// 使い方(loop() のポーリングに合わせて、毎フレーム poll() を「falseが返るまで」回す):
//
//     ws.begin("wss://example.com/api/websocket");
//     ...
//     WebSocketClient::Event ev;
//     while(ws.poll(ev)){
//         if(ev.type == WebSocketClient::EventType::Message) ...  // ev.data/ev.len は次の poll() まで有効
//     }
//
// - begin() が成功したら、最後に必ず1回だけ Closed が来る(abort() したときを除く)
// - **接続とTLSのハンドシェイクは最初の poll() の中で同期的に進む**(HttpGet と同じ割り切り。最大3秒、
//   TLSは1〜2秒画面が止まる)。開始の挨拶(HTTPのUpgrade)の応答待ち以降はポーリングで止めない
// - pingは自動で答える。こちらからも ping_interval ごとに送り、次の間隔までに何も届かなければ切る
//   (家のルーターのNATが黙って経路を忘れる対策)
// - メッセージは begin() で決めた上限(既定8KiB)まで。超えたら 1009 で閉じる。テキストのUTF-8は確かめない
// - 圧縮(permessage-deflate)等の拡張は申し出ない
//
// 持ち物: 受信の溜め場所1KiB + メッセージの溜め場所(begin() で new。閉じたら返す)。wss:// なら接続の間TLSの約40KB
class WebSocketClient {
    public:
        static constexpr unsigned long kConnectTimeoutMs = 3000;
        static constexpr unsigned long kHandshakeTimeoutMs = 10000;
        // こちらから閉じたとき、相手のcloseを待つ時間
        static constexpr unsigned long kCloseWaitMs = 2000;
        static constexpr unsigned long kDefaultPingIntervalMs = 30000;
        static constexpr size_t kReadPerPoll = 1024;
        static constexpr size_t kDefaultMaxMessage = PICO_STR_8KiB;
        // 開始の挨拶の応答もメッセージの溜め場所で受けるので、これより小さくはしない
        static constexpr size_t kMinMaxMessage = PICO_STR_1KiB;
        static constexpr size_t kMaxMaxMessage = 64u * 1024u;
        // 足すヘッダの合計(Authorization 等)
        static constexpr size_t kMaxExtraHeaderBytes = PICO_STR_512B;

        enum class State : uint8_t { Idle, Connecting, Handshake, Open, Closing, Closed };
        enum class EventType : uint8_t { Open, Message, Closed };

        struct Event {
            EventType type = EventType::Open;
            // Message: 本文 / Closed: 相手が送ってきた理由(無ければ長さ0)
            const uint8_t* data = nullptr;
            size_t len = 0;
            bool binary = false;
            // Closed: 閉じた番号(WsFrame::kClose*。closeを受けずに切れたら 1006)
            uint16_t code = 0;
            // Closed: こちらの事情(繋がらない・規約違反・応答が無い等)で閉じたときの理由。正常に閉じたらnullptr
            const char* error = nullptr;
        };

        WebSocketClient() = default;
        ~WebSocketClient(){ abort(); }

        WebSocketClient(const WebSocketClient&) = delete;
        WebSocketClient& operator=(const WebSocketClient&) = delete;

        // ws:// / wss:// のURLへ繋ぎ始める(実際に繋ぐのは最初の poll())。URLが不正・メモリが無ければfalse
        bool begin(const char* url, size_t max_message = kDefaultMaxMessage);
        // 開始の挨拶へ足すもの。begin() より前に決める(begin() は消さない。消すのは clearHeaders())
        bool setProtocol(const char* protocol);
        bool addHeader(const char* name, const char* value);
        void clearHeaders(){ extra_headers_.clear(); protocol_.clear(); }
        // 0 = こちらからpingを送らない
        void setPingInterval(unsigned long ms){ ping_interval_ms_ = ms; }

        // 進める。イベントが1つあれば ev に入れて true(続けて呼ぶ)。何も無ければ false
        bool poll(Event& ev);

        // Open の間だけ送れる。送れなければfalse(相手が切れていた等。その後 Closed が来る)
        bool sendText(const void* data, size_t len){ return sendFrame(WsFrame::Text, data, len); }
        bool sendBinary(const void* data, size_t len){ return sendFrame(WsFrame::Binary, data, len); }
        bool sendPing(const void* data = nullptr, size_t len = 0){ return sendFrame(WsFrame::Ping, data, len); }

        // 閉じる挨拶を送る(相手の返事か kCloseWaitMs で Closed が来る)。開く前なら切って Closed
        void close(uint16_t code = WsFrame::kCloseNormal, const char* reason = nullptr);
        // 何も送らずに切る。Closed は来ない
        void abort();

        State state() const { return state_; }
        bool isOpen() const { return state_ == State::Open; }
        // サーバが選んだサブプロトコル(Sec-WebSocket-Protocol。無ければ空)
        const char* protocol() const { return accepted_protocol_.c_str(); }
        const Url& url() const { return url_; }

    private:
        HttpTransport conn_;
        WsFrame::Parser parser_;
        Url url_;
        State state_ = State::Idle;

        uint8_t* msg_ = nullptr;     // メッセージの溜め場所(開始の挨拶の応答もここで受ける)
        size_t msg_cap_ = 0;
        size_t hs_len_ = 0;          // 開始の挨拶の応答を受けたバイト数

        uint8_t rx_[kReadPerPoll];
        size_t rx_pos_ = 0;
        size_t rx_len_ = 0;

        char key_[WsFrame::kKeyChars + 1] = {};
        FixedString<PICO_STR_M> protocol_;
        FixedString<PICO_STR_M> accepted_protocol_;
        FixedString<kMaxExtraHeaderBytes> extra_headers_;   // "Name: value\r\n" を並べたもの

        unsigned long started_ms_ = 0;
        unsigned long last_rx_ms_ = 0;
        unsigned long last_ping_ms_ = 0;
        unsigned long ping_interval_ms_ = kDefaultPingIntervalMs;
        bool ping_waiting_ = false;

        // 最後の Closed の中身(閉じた直後の poll() で渡す)
        bool closed_pending_ = false;
        uint16_t closed_code_ = 0;
        const char* closed_error_ = nullptr;
        uint8_t closed_reason_[WsFrame::kMaxControlPayload] = {};
        size_t closed_reason_len_ = 0;

        bool sendHandshake();
        // 戻り: 1 = そろった / 0 = まだ / -1 = 失敗(closed_error_ を立てて切った)
        int readHandshake();
        bool checkHandshake(size_t header_len);
        bool sendFrame(WsFrame::Opcode op, const void* data, size_t len);
        bool sendClose(uint16_t code, const uint8_t* reason, size_t reason_len);
        // 切って、次の poll() で Closed を渡す
        void finish(uint16_t code, const char* error, const uint8_t* reason = nullptr, size_t reason_len = 0);
        void releaseBuffer();
        bool pollOpen(Event& ev);
};
