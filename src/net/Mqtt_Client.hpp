#pragma once

#include "net/Http_Transport.hpp"
#include "net/Mqtt_Packet.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstddef>
#include <cstdint>

// MQTT 3.1.1 のクライアント。mqtt://host[:1883] は素のTCP、mqtts://host[:8883] はTLS(net/Http_Transport)。
// Home Assistant(Mosquittoアドオン)・IoT機器のリモコン等、話題(トピック)を購読して即座に受け取るのに使う。
//
// 使い方は WebSocketClient と同じ(毎フレーム poll() を false が返るまで回す)。
//
// - begin() が成功したら、最後に必ず1回だけ Closed が来る(abort() したときを除く)
// - 接続とTLSのハンドシェイクは最初の poll() の中で同期的に進む(最大3秒。TLSは1〜2秒画面が止まる)
// - 送れるのは QoS 0/1。QoS 1 は PUBACK が来たら Published が来る(送り直しはしない。切れたら届いたか分からない)
// - 受けるのは QoS 0/1/2(2は PUBREC/PUBCOMP まで答える。同じものが2回届くことはありうる)
// - keepalive の間に何も送っていなければ PINGREQ を送り、1.5倍の間何も届かなければ切る
// - 溜め場所(begin() で new)より大きいパケットは読み捨てる(QoS 1/2 なら受け取ったと答える。dropped() が増える)
// - 切れても自動では繋ぎ直さない(呼び出し側が Closed を見て begin() し直す)
class MqttClient {
    public:
        static constexpr unsigned long kConnectTimeoutMs = 3000;
        static constexpr unsigned long kConnackTimeoutMs = 10000;
        static constexpr size_t kReadPerPoll = 512;
        static constexpr size_t kDefaultMaxPacket = PICO_STR_4KiB;
        static constexpr size_t kMinMaxPacket = PICO_STR_1KiB;
        static constexpr size_t kMaxMaxPacket = 64u * 1024u;

        enum class State : uint8_t { Idle, Connecting, WaitConnack, Connected, Closed };
        enum class EventType : uint8_t {
            Connected,     // CONNACKで受け入れられた(session_present)
            Message,       // 購読した話題に届いた(topic/payload/qos/retain)
            Subscribed,    // SUBACK(packet_id / granted_qos。0x80 = 断られた)
            Unsubscribed,  // UNSUBACK(packet_id)
            Published,     // 送ったQoS 1が届いた(PUBACK。packet_id)
            Closed,        // 切れた(error が nullptr なら disconnect() で閉じた)
        };

        struct Event {
            EventType type = EventType::Connected;
            const uint8_t* topic = nullptr;    // Message: 次の poll() まで有効
            size_t topic_len = 0;
            const uint8_t* payload = nullptr;
            size_t payload_len = 0;
            uint8_t qos = 0;
            bool retain = false;
            uint16_t packet_id = 0;
            uint8_t granted_qos = 0;
            bool session_present = false;
            // Closed: こちらの事情・断られた理由(日本語)。disconnect() で閉じたら nullptr
            const char* error = nullptr;
        };

        struct Options {
            FixedString<PICO_STR_M> client_id;      // 空ならブローカが名前を付ける(clean_session のときだけ)
            FixedString<PICO_STR_L> username;       // 空なら送らない
            FixedString<PICO_STR_256B> password;    // 空なら送らない(Home Assistantの長期トークンも入る)
            bool has_password = false;              // 空のパスワードを送りたいとき用
            uint16_t keepalive_s = 60;
            bool clean_session = true;
            FixedString<MqttPacket::kMaxTopicBytes + 1> will_topic; // 空なら遺言なし
            FixedString<PICO_STR_256B> will_payload;
            uint8_t will_qos = 0;
            bool will_retain = false;
        };

        MqttClient() = default;
        ~MqttClient(){ abort(); }

        MqttClient(const MqttClient&) = delete;
        MqttClient& operator=(const MqttClient&) = delete;

        // URLが不正・メモリが無ければfalse(実際に繋ぐのは最初の poll())
        bool begin(const char* url, const Options& opts, size_t max_packet = kDefaultMaxPacket);
        bool poll(Event& ev);

        // 繋がっている間だけ送れる。戻り: QoS 1/2 ならパケット識別子(1〜65535)、QoS 0 なら0、送れなければ -1
        int32_t publish(const char* topic, const void* payload, size_t len, uint8_t qos = 0, bool retain = false);
        int32_t subscribe(const char* topic, uint8_t qos = 0);
        int32_t unsubscribe(const char* topic);

        // DISCONNECT を送って切る(次の poll() で Closed、error = nullptr)
        void disconnect();
        // 何も送らずに切る。Closed は来ない
        void abort();

        State state() const { return state_; }
        bool isConnected() const { return state_ == State::Connected; }
        // 大きすぎて読み捨てたパケットの数
        uint32_t dropped() const { return dropped_; }
        const Url& url() const { return url_; }

        // "mqtt://" / "mqtts://" のURLを分解する(ポートの既定は1883 / 8883。パスは使わない)
        static bool ParseUrl(Url& out, const char* url);

    private:
        HttpTransport conn_;
        MqttPacket::Parser parser_;
        Url url_;
        State state_ = State::Idle;
        Options opts_;

        uint8_t* buf_ = nullptr;     // 受けたパケットの中身(CONNECTの組み立てにも使う)
        size_t cap_ = 0;
        uint8_t rx_[kReadPerPoll];
        size_t rx_pos_ = 0;
        size_t rx_len_ = 0;

        uint16_t next_id_ = 1;
        unsigned long started_ms_ = 0;
        unsigned long last_tx_ms_ = 0;
        unsigned long last_rx_ms_ = 0;
        uint32_t dropped_ = 0;

        bool closed_pending_ = false;
        const char* closed_error_ = nullptr;

        uint16_t nextId();
        bool sendRaw(const uint8_t* p, size_t n);
        bool sendConnect();
        void finish(const char* error);
        void release();
        // そろったパケットを1つ片付ける。渡すイベントがあれば true
        bool handlePacket(Event& ev);
        bool pollConnected(Event& ev);
};
