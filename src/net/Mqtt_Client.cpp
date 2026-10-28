#include "net/Mqtt_Client.hpp"

#include "system/Log_Functions.hpp"

#include <Arduino.h>
#include <cstring>
#include <new>

namespace {
    // CONNACK の返事の番号(3.2.2.3)
    const char* ConnackText(uint8_t code){
        switch(code){
            case 1: return "ブローカがMQTT 3.1.1を話せない";
            case 2: return "クライアントIDを断られた";
            case 3: return "ブローカが使えない状態";
            case 4: return "ユーザー名かパスワードが違う";
            case 5: return "接続を許されていない(認証)";
            default: return "ブローカに断られた";
        }
    }
}

bool MqttClient::ParseUrl(Url& out, const char* url){
    if(!url) return false;
    const char* rest = nullptr;
    bool secure = false;
    if(strncmp(url, "mqtt://", 7) == 0){
        rest = url + 7;
    }else if(strncmp(url, "mqtts://", 8) == 0){
        rest = url + 8;
        secure = true;
    }else{
        return false;
    }
    FixedString<PICO_STR_512B> http_url;
    if(!http_url.assign(secure ? "https://" : "http://") || !http_url.append(rest)) return false;
    if(!UrlTools::Parse(out, http_url.c_str())) return false;
    //ポートを書いていなければMQTTの既定(Url は 80/443 を入れてくる)
    bool has_port = false;
    for(const char* p = rest; *p && *p != '/' && *p != '?'; p++){
        if(*p == ':'){ has_port = true; break; }
    }
    if(!has_port) out.port = secure ? 8883 : 1883;
    return true;
}

bool MqttClient::begin(const char* url, const Options& opts, size_t max_packet){
    abort();
    if(!ParseUrl(url_, url)) return false;
    if(opts.client_id.empty() && !opts.clean_session) return false;
    if(!opts.will_topic.empty() && (!MqttPacket::ValidTopic(opts.will_topic.c_str(), false) || opts.will_qos > 2)){
        return false;
    }
    if(max_packet < kMinMaxPacket) max_packet = kMinMaxPacket;
    if(max_packet > kMaxMaxPacket) max_packet = kMaxMaxPacket;
    buf_ = new (std::nothrow) uint8_t[max_packet];
    if(!buf_) return false;
    cap_ = max_packet;
    opts_ = opts;
    rx_pos_ = rx_len_ = 0;
    dropped_ = 0;
    closed_pending_ = false;
    closed_error_ = nullptr;
    state_ = State::Connecting;
    return true;
}

uint16_t MqttClient::nextId(){
    const uint16_t id = next_id_;
    next_id_ = (uint16_t)(next_id_ + 1);
    if(next_id_ == 0) next_id_ = 1;
    return id;
}

bool MqttClient::sendRaw(const uint8_t* p, size_t n){
    if(conn_.write(p, n) != n){
        finish("送れない(切れた)");
        return false;
    }
    last_tx_ms_ = millis();
    return true;
}

bool MqttClient::sendConnect(){
    MqttPacket::ConnectOptions c;
    c.client_id = opts_.client_id.c_str();
    c.username = opts_.username.empty() ? nullptr : opts_.username.c_str();
    c.password = (opts_.has_password || !opts_.password.empty()) ? opts_.password.c_str() : nullptr;
    c.keepalive_s = opts_.keepalive_s;
    c.clean_session = opts_.clean_session;
    if(!opts_.will_topic.empty()){
        c.will_topic = opts_.will_topic.c_str();
        c.will_payload = (const uint8_t*)opts_.will_payload.c_str();
        c.will_len = opts_.will_payload.length();
        c.will_qos = opts_.will_qos;
        c.will_retain = opts_.will_retain;
    }
    //まだ何も受けていないので、受けの溜め場所で組み立てる
    const size_t n = MqttPacket::EncodeConnect(buf_, cap_, c);
    if(n == 0) return false;
    return conn_.write(buf_, n) == n;
}

void MqttClient::release(){
    delete[] buf_;
    buf_ = nullptr;
    cap_ = 0;
    parser_.reset(nullptr, 0);
}

void MqttClient::abort(){
    conn_.close();
    release();
    state_ = State::Idle;
    closed_pending_ = false;
}

void MqttClient::finish(const char* error){
    if(state_ == State::Idle || state_ == State::Closed) return;
    conn_.close();
    closed_pending_ = true;
    closed_error_ = error;
    state_ = State::Closed;
}

void MqttClient::disconnect(){
    if(state_ == State::Connected){
        //読み残し(届いたばかりのPUBACK等)を抱えたまま閉じると、TCPはRSTを送ってDISCONNECTが捨てられうる。
        //そうなるとブローカは「異常な切断」とみなして遺言を流すので、先に読み捨ててから送る
        while(conn_.available() > 0){
            if(conn_.read(rx_, sizeof(rx_)) <= 0) break;
        }
        uint8_t p[2];
        MqttPacket::EncodeEmpty(p, MqttPacket::DISCONNECT);
        conn_.write(p, sizeof(p));
    }
    finish(nullptr);
}

int32_t MqttClient::publish(const char* topic, const void* payload, size_t len, uint8_t qos, bool retain){
    if(state_ != State::Connected || qos > 2) return -1;
    if(!MqttPacket::ValidTopic(topic, false)) return -1;
    const uint16_t id = qos > 0 ? nextId() : 0;
    uint8_t head[MqttPacket::kMaxFixedHeader + 2 + MqttPacket::kMaxTopicBytes + 2];
    const size_t hn = MqttPacket::EncodePublishHeader(head, sizeof(head), topic, len, qos, retain, false, id);
    if(hn == 0) return -1;
    if(!sendRaw(head, hn)) return -1;
    if(len > 0 && !sendRaw((const uint8_t*)payload, len)) return -1;
    return id;
}

int32_t MqttClient::subscribe(const char* topic, uint8_t qos){
    if(state_ != State::Connected || qos > 2) return -1;
    if(!MqttPacket::ValidTopic(topic, true)) return -1;
    const uint16_t id = nextId();
    uint8_t p[MqttPacket::kMaxFixedHeader + 2 + 2 + MqttPacket::kMaxTopicBytes + 1];
    const size_t n = MqttPacket::EncodeSubscribe(p, sizeof(p), id, topic, qos);
    if(n == 0 || !sendRaw(p, n)) return -1;
    return id;
}

int32_t MqttClient::unsubscribe(const char* topic){
    if(state_ != State::Connected) return -1;
    if(!MqttPacket::ValidTopic(topic, true)) return -1;
    const uint16_t id = nextId();
    uint8_t p[MqttPacket::kMaxFixedHeader + 2 + 2 + MqttPacket::kMaxTopicBytes];
    const size_t n = MqttPacket::EncodeUnsubscribe(p, sizeof(p), id, topic);
    if(n == 0 || !sendRaw(p, n)) return -1;
    return id;
}

bool MqttClient::poll(Event& ev){
    ev = Event{};
    if(closed_pending_){
        closed_pending_ = false;
        release();
        ev.type = EventType::Closed;
        ev.error = closed_error_;
        return true;
    }
    switch(state_){
        case State::Connecting:
            if(!conn_.connect(url_, kConnectTimeoutMs)){
                LOG_SYS_WARN("MQTT: %s:%u へ繋げません (%s)", url_.host.c_str(), (unsigned)url_.port, conn_.errorText());
                finish(conn_.errorText());
                return poll(ev);
            }
            if(!sendConnect()){
                finish("CONNECTを送れない(長すぎる/切れた)");
                return poll(ev);
            }
            parser_.reset(buf_, cap_);
            state_ = State::WaitConnack;
            started_ms_ = last_tx_ms_ = last_rx_ms_ = millis();
            return false;
        case State::WaitConnack:
        case State::Connected:
            return pollConnected(ev);
        default:
            return false;
    }
}

bool MqttClient::handlePacket(Event& ev){
    using namespace MqttPacket;
    const uint8_t* body = parser_.body();
    const size_t len = parser_.bodyLen();

    if(state_ == State::WaitConnack){
        if(parser_.type() != CONNACK || len < 2){
            finish("ブローカの返事がCONNACKでない");
            return poll(ev);
        }
        if(body[1] != 0){
            LOG_SYS_WARN("MQTT: %s に断られました (CONNACK %u)", url_.host.c_str(), (unsigned)body[1]);
            finish(ConnackText(body[1]));
            return poll(ev);
        }
        state_ = State::Connected;
        ev.type = EventType::Connected;
        ev.session_present = (body[0] & 0x01) != 0;
        return true;
    }

    switch(parser_.type()){
        case PUBLISH: {
            Publish pub;
            if(!ParsePublish(parser_.flags(), body, len, pub)){
                finish("ブローカから壊れたPUBLISHが届いた");
                return poll(ev);
            }
            //受け取ったと答える(読み捨てるものにも答える。答えないとブローカが送り直し続ける)
            if(pub.qos == 1 || pub.qos == 2){
                uint8_t ack[4];
                EncodeAck(ack, pub.qos == 1 ? PUBACK : PUBREC, pub.packet_id);
                if(!sendRaw(ack, sizeof(ack))) return poll(ev);
            }
            if(parser_.truncated()){
                dropped_++;
                LOG_SYS_WARN("MQTT: 大きすぎるメッセージを読み捨てました (%u B > %u B)",
                             (unsigned)parser_.remainingLength(), (unsigned)cap_);
                return false;
            }
            ev.type = EventType::Message;
            ev.topic = pub.topic;
            ev.topic_len = pub.topic_len;
            ev.payload = pub.payload;
            ev.payload_len = pub.payload_len;
            ev.qos = pub.qos;
            ev.retain = pub.retain;
            ev.packet_id = pub.packet_id;
            return true;
        }
        case PUBREL: {
            uint16_t id = 0;
            if(ParsePacketId(body, len, id)){
                uint8_t ack[4];
                EncodeAck(ack, PUBCOMP, id);
                if(!sendRaw(ack, sizeof(ack))) return poll(ev);
            }
            return false;
        }
        case PUBREC: {
            //送ったQoS 2の1段目。PUBRELで続ける
            uint16_t id = 0;
            if(ParsePacketId(body, len, id)){
                uint8_t ack[4];
                EncodeAck(ack, PUBREL, id);
                if(!sendRaw(ack, sizeof(ack))) return poll(ev);
            }
            return false;
        }
        case PUBACK:
        case PUBCOMP: {
            uint16_t id = 0;
            if(!ParsePacketId(body, len, id)) return false;
            ev.type = EventType::Published;
            ev.packet_id = id;
            return true;
        }
        case SUBACK: {
            uint16_t id = 0;
            if(!ParsePacketId(body, len, id) || len < 3) return false;
            ev.type = EventType::Subscribed;
            ev.packet_id = id;
            ev.granted_qos = body[2];
            return true;
        }
        case UNSUBACK: {
            uint16_t id = 0;
            if(!ParsePacketId(body, len, id)) return false;
            ev.type = EventType::Unsubscribed;
            ev.packet_id = id;
            return true;
        }
        case PINGRESP:
            return false;
        default:
            finish("ブローカから想定外のパケットが届いた");
            return poll(ev);
    }
}

bool MqttClient::pollConnected(Event& ev){
    bool read_done = false;
    while(true){
        if(rx_pos_ < rx_len_){
            size_t used = 0;
            const MqttPacket::Parser::Result r = parser_.feed(rx_ + rx_pos_, rx_len_ - rx_pos_, used);
            rx_pos_ += used;
            if(r == MqttPacket::Parser::Result::Error){
                finish("ブローカから壊れたパケットが届いた");
                return poll(ev);
            }
            if(r == MqttPacket::Parser::Result::Packet){
                if(handlePacket(ev)) return true;
                if(state_ != State::Connected && state_ != State::WaitConnack) return poll(ev);
                continue;
            }
        }
        if(read_done) break;
        read_done = true;
        rx_pos_ = rx_len_ = 0;
        if(conn_.available() > 0){
            const int n = conn_.read(rx_, sizeof(rx_));
            if(n > 0){
                rx_len_ = (size_t)n;
                last_rx_ms_ = millis();
                continue;
            }
        }
        if(!conn_.connected()){
            finish("接続が切れた");
            return poll(ev);
        }
        break;
    }

    const unsigned long now = millis();
    if(state_ == State::WaitConnack){
        if(now - started_ms_ >= kConnackTimeoutMs){
            finish("ブローカの返事が無い(CONNACK)");
            return poll(ev);
        }
        return false;
    }
    if(opts_.keepalive_s > 0){
        const unsigned long ka = (unsigned long)opts_.keepalive_s * 1000UL;
        if(now - last_rx_ms_ >= ka + ka / 2){
            finish("ブローカの返事が無い(keepalive)");
            return poll(ev);
        }
        if(now - last_tx_ms_ >= ka){
            uint8_t p[2];
            MqttPacket::EncodeEmpty(p, MqttPacket::PINGREQ);
            if(!sendRaw(p, sizeof(p))) return poll(ev);
        }
    }
    return false;
}
