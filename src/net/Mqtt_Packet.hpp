#pragma once

#include <cstddef>
#include <cstdint>

// MQTT 3.1.1 の組み立てと読み取り。通信はしない(net/Mqtt_Client が使う)。
//
// 組み立ては呼び出し側の溜め場所へ書き、書いたバイト数を返す(入りきらなければ0)。
// 読み取りは受け取ったバイト列から1パケットずつ切り出す(溜め場所は呼び出し側が渡す固定長)。
// 確保はしない。
namespace MqttPacket {
    enum Type : uint8_t {
        CONNECT = 1, CONNACK = 2, PUBLISH = 3, PUBACK = 4, PUBREC = 5, PUBREL = 6, PUBCOMP = 7,
        SUBSCRIBE = 8, SUBACK = 9, UNSUBSCRIBE = 10, UNSUBACK = 11, PINGREQ = 12, PINGRESP = 13,
        DISCONNECT = 14,
    };

    // 残りの長さ(Remaining Length)の上限(4バイトの可変長で表せる最大)
    constexpr uint32_t kMaxRemaining = 268435455u;
    constexpr size_t kMaxFixedHeader = 5;
    // 話題(トピック)の長さの上限。仕様は65535だが、組み立てをスタックで済ませるために絞る
    constexpr size_t kMaxTopicBytes = 256;

    struct ConnectOptions {
        const char* client_id = "";     // 空なら clean_session=true のときだけ許される(ブローカが名前を付ける)
        const char* username = nullptr; // nullptr = 送らない
        const char* password = nullptr; // nullptr = 送らない(username が無いときは送れない)
        uint16_t keepalive_s = 60;
        bool clean_session = true;
        // 遺言(切れたときにブローカが代わりに流すもの)。will_topic が nullptr なら無し
        const char* will_topic = nullptr;
        const uint8_t* will_payload = nullptr;
        size_t will_len = 0;
        uint8_t will_qos = 0;
        bool will_retain = false;
    };

    // 残りの長さを書く(1〜4バイト)。書いたバイト数
    size_t EncodeRemaining(uint8_t* out, uint32_t len);

    size_t EncodeConnect(uint8_t* out, size_t cap, const ConnectOptions& o);
    // PUBLISH の本文より前(固定ヘッダ・話題・パケット識別子)。本文は続けてそのまま送る。
    // out は kMaxFixedHeader + 2 + kMaxTopicBytes + 2 バイトあれば足りる
    size_t EncodePublishHeader(uint8_t* out, size_t cap, const char* topic, size_t payload_len,
                               uint8_t qos, bool retain, bool dup, uint16_t packet_id);
    size_t EncodeSubscribe(uint8_t* out, size_t cap, uint16_t packet_id, const char* topic, uint8_t qos);
    size_t EncodeUnsubscribe(uint8_t* out, size_t cap, uint16_t packet_id, const char* topic);
    // PUBACK / PUBREC / PUBREL / PUBCOMP(4バイト)
    size_t EncodeAck(uint8_t* out, Type type, uint16_t packet_id);
    // PINGREQ / DISCONNECT(2バイト)
    size_t EncodeEmpty(uint8_t* out, Type type);

    // 話題が送ってよい形か(空・ワイルドカードの位置・NUL等)。publish は + # を使えない
    bool ValidTopic(const char* topic, bool allow_wildcards);

    // 受け取った PUBLISH の中身
    struct Publish {
        const uint8_t* topic = nullptr;
        size_t topic_len = 0;
        const uint8_t* payload = nullptr;
        size_t payload_len = 0;
        uint16_t packet_id = 0;
        uint8_t qos = 0;
        bool retain = false;
        bool dup = false;
    };
    // flags = 固定ヘッダの下位4ビット。body が途中で切れている(truncated)ときも話題と識別子までは読める
    bool ParsePublish(uint8_t flags, const uint8_t* body, size_t len, Publish& out);
    // 先頭2バイトのパケット識別子(PUBACK/SUBACK/UNSUBACK 等)
    bool ParsePacketId(const uint8_t* body, size_t len, uint16_t& id);

    // 受け取ったバイト列から1パケットずつ切り出す
    class Parser {
        public:
            enum class Result : uint8_t {
                NeedMore,
                Packet,    // 1つそろった: type()/flags()/body()/bodyLen()/truncated()
                Error,     // 残りの長さが不正
            };

            // buf = 中身を入れる溜め場所(非所有)。cap を超えた分は読み捨て、truncated() が立つ
            void reset(uint8_t* buf, size_t cap);
            Result feed(const uint8_t* p, size_t n, size_t& consumed);

            Type type() const { return (Type)(hdr_ >> 4); }
            uint8_t flags() const { return hdr_ & 0x0F; }
            const uint8_t* body() const { return buf_; }
            size_t bodyLen() const { return stored_; }
            // 中身が溜め場所に入りきらなかった(本当の長さは remainingLength())
            bool truncated() const { return stored_ < (size_t)remaining_; }
            uint32_t remainingLength() const { return remaining_; }

        private:
            enum class Phase : uint8_t { Type, Length, Body };
            uint8_t* buf_ = nullptr;
            size_t cap_ = 0;
            Phase phase_ = Phase::Type;
            uint8_t hdr_ = 0;
            uint32_t remaining_ = 0;
            uint32_t got_ = 0;
            size_t stored_ = 0;
            uint32_t mult_ = 1;
            int len_bytes_ = 0;
    };
}
