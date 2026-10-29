#include "net/Mqtt_Packet.hpp"

#include <cstring>

namespace {
    // 長さ2バイト + 文字列
    size_t PutString(uint8_t* out, const void* s, size_t len){
        out[0] = (uint8_t)(len >> 8);
        out[1] = (uint8_t)len;
        if(len) memcpy(out + 2, s, len);
        return 2 + len;
    }

    size_t RemainingBytes(uint32_t len){
        if(len < 128) return 1;
        if(len < 16384) return 2;
        if(len < 2097152) return 3;
        return 4;
    }
}

size_t MqttPacket::EncodeRemaining(uint8_t* out, uint32_t len){
    size_t n = 0;
    do{
        uint8_t b = (uint8_t)(len % 128);
        len /= 128;
        if(len > 0) b |= 0x80;
        out[n++] = b;
    }while(len > 0 && n < 4);
    return n;
}

size_t MqttPacket::EncodeConnect(uint8_t* out, size_t cap, const ConnectOptions& o){
    const char* cid = o.client_id ? o.client_id : "";
    const size_t cid_len = strlen(cid);
    if(cid_len == 0 && !o.clean_session) return 0; //名前の無い持ち越しのセッションは作れない(3.1.3.1)
    if(o.password && !o.username) return 0;        //3.1.2.9
    if(o.will_qos > 2) return 0;
    if(cid_len > 0xFFFF) return 0;

    size_t rem = 10 + 2 + cid_len; //可変ヘッダ10バイト + client id
    size_t wt_len = 0;
    if(o.will_topic){
        wt_len = strlen(o.will_topic);
        if(wt_len == 0 || wt_len > 0xFFFF || o.will_len > 0xFFFF) return 0;
        rem += 2 + wt_len + 2 + o.will_len;
    }
    const size_t user_len = o.username ? strlen(o.username) : 0;
    const size_t pass_len = o.password ? strlen(o.password) : 0;
    if(user_len > 0xFFFF || pass_len > 0xFFFF) return 0;
    if(o.username) rem += 2 + user_len;
    if(o.password) rem += 2 + pass_len;

    const size_t total = 1 + RemainingBytes((uint32_t)rem) + rem;
    if(total > cap) return 0;

    size_t n = 0;
    out[n++] = (uint8_t)(CONNECT << 4);
    n += EncodeRemaining(out + n, (uint32_t)rem);
    n += PutString(out + n, "MQTT", 4);
    out[n++] = 4; //3.1.1
    uint8_t flags = 0;
    if(o.clean_session) flags |= 0x02;
    if(o.will_topic){
        flags |= 0x04;
        flags |= (uint8_t)(o.will_qos << 3);
        if(o.will_retain) flags |= 0x20;
    }
    if(o.password) flags |= 0x40;
    if(o.username) flags |= 0x80;
    out[n++] = flags;
    out[n++] = (uint8_t)(o.keepalive_s >> 8);
    out[n++] = (uint8_t)o.keepalive_s;
    n += PutString(out + n, cid, cid_len);
    if(o.will_topic){
        n += PutString(out + n, o.will_topic, wt_len);
        n += PutString(out + n, o.will_payload, o.will_len);
    }
    if(o.username) n += PutString(out + n, o.username, user_len);
    if(o.password) n += PutString(out + n, o.password, pass_len);
    return n;
}

size_t MqttPacket::EncodePublishHeader(uint8_t* out, size_t cap, const char* topic, size_t payload_len,
                                       uint8_t qos, bool retain, bool dup, uint16_t packet_id){
    if(qos > 2 || !topic) return 0;
    const size_t tlen = strlen(topic);
    if(tlen == 0 || tlen > kMaxTopicBytes) return 0;
    if(qos > 0 && packet_id == 0) return 0;
    const size_t var = 2 + tlen + (qos > 0 ? 2 : 0);
    if(payload_len > kMaxRemaining - var) return 0;
    const uint32_t rem = (uint32_t)(var + payload_len);
    if(1 + RemainingBytes(rem) + var > cap) return 0;

    size_t n = 0;
    out[n++] = (uint8_t)((PUBLISH << 4) | (dup ? 0x08 : 0) | (qos << 1) | (retain ? 0x01 : 0));
    n += EncodeRemaining(out + n, rem);
    n += PutString(out + n, topic, tlen);
    if(qos > 0){
        out[n++] = (uint8_t)(packet_id >> 8);
        out[n++] = (uint8_t)packet_id;
    }
    return n;
}

size_t MqttPacket::EncodeSubscribe(uint8_t* out, size_t cap, uint16_t packet_id, const char* topic, uint8_t qos){
    if(!topic || qos > 2 || packet_id == 0) return 0;
    const size_t tlen = strlen(topic);
    if(tlen == 0 || tlen > kMaxTopicBytes) return 0;
    const uint32_t rem = (uint32_t)(2 + 2 + tlen + 1);
    if(1 + RemainingBytes(rem) + rem > cap) return 0;
    size_t n = 0;
    out[n++] = (uint8_t)((SUBSCRIBE << 4) | 0x02); //下位ビットは0010と決まっている(3.8.1)
    n += EncodeRemaining(out + n, rem);
    out[n++] = (uint8_t)(packet_id >> 8);
    out[n++] = (uint8_t)packet_id;
    n += PutString(out + n, topic, tlen);
    out[n++] = qos;
    return n;
}

size_t MqttPacket::EncodeUnsubscribe(uint8_t* out, size_t cap, uint16_t packet_id, const char* topic){
    if(!topic || packet_id == 0) return 0;
    const size_t tlen = strlen(topic);
    if(tlen == 0 || tlen > kMaxTopicBytes) return 0;
    const uint32_t rem = (uint32_t)(2 + 2 + tlen);
    if(1 + RemainingBytes(rem) + rem > cap) return 0;
    size_t n = 0;
    out[n++] = (uint8_t)((UNSUBSCRIBE << 4) | 0x02);
    n += EncodeRemaining(out + n, rem);
    out[n++] = (uint8_t)(packet_id >> 8);
    out[n++] = (uint8_t)packet_id;
    n += PutString(out + n, topic, tlen);
    return n;
}

size_t MqttPacket::EncodeAck(uint8_t* out, Type type, uint16_t packet_id){
    //PUBRELだけは下位ビットが0010(3.6.1)
    out[0] = (uint8_t)((type << 4) | (type == PUBREL ? 0x02 : 0));
    out[1] = 2;
    out[2] = (uint8_t)(packet_id >> 8);
    out[3] = (uint8_t)packet_id;
    return 4;
}

size_t MqttPacket::EncodeEmpty(uint8_t* out, Type type){
    out[0] = (uint8_t)(type << 4);
    out[1] = 0;
    return 2;
}

bool MqttPacket::ValidTopic(const char* topic, bool allow_wildcards){
    if(!topic || !*topic) return false;
    const size_t len = strlen(topic);
    if(len > kMaxTopicBytes) return false;
    for(size_t i = 0; i < len; i++){
        const char c = topic[i];
        if(c == '+' || c == '#'){
            if(!allow_wildcards) return false;
            //ワイルドカードは段(/区切り)まるごとでなければならない。#は最後だけ
            const bool start_ok = (i == 0 || topic[i - 1] == '/');
            const bool end_ok = (i + 1 == len || topic[i + 1] == '/');
            if(!start_ok || !end_ok) return false;
            if(c == '#' && i + 1 != len) return false;
        }
    }
    return true;
}

bool MqttPacket::ParsePublish(uint8_t flags, const uint8_t* body, size_t len, Publish& out){
    out = Publish{};
    out.qos = (flags >> 1) & 0x03;
    out.retain = (flags & 0x01) != 0;
    out.dup = (flags & 0x08) != 0;
    if(out.qos > 2) return false;
    if(len < 2) return false;
    const size_t tlen = ((size_t)body[0] << 8) | body[1];
    size_t pos = 2 + tlen;
    if(pos > len) return false;
    out.topic = body + 2;
    out.topic_len = tlen;
    if(out.qos > 0){
        if(pos + 2 > len) return false;
        out.packet_id = (uint16_t)((body[pos] << 8) | body[pos + 1]);
        pos += 2;
    }
    out.payload = body + pos;
    out.payload_len = len - pos;
    return true;
}

bool MqttPacket::ParsePacketId(const uint8_t* body, size_t len, uint16_t& id){
    if(len < 2) return false;
    id = (uint16_t)((body[0] << 8) | body[1]);
    return true;
}

void MqttPacket::Parser::reset(uint8_t* buf, size_t cap){
    *this = Parser{};
    buf_ = buf;
    cap_ = cap;
}

MqttPacket::Parser::Result MqttPacket::Parser::feed(const uint8_t* p, size_t n, size_t& consumed){
    consumed = 0;
    while(consumed < n || (phase_ == Phase::Body && got_ == remaining_)){
        switch(phase_){
            case Phase::Type:
                hdr_ = p[consumed++];
                remaining_ = 0;
                mult_ = 1;
                len_bytes_ = 0;
                got_ = 0;
                stored_ = 0;
                phase_ = Phase::Length;
                break;
            case Phase::Length: {
                const uint8_t b = p[consumed++];
                remaining_ += (uint32_t)(b & 0x7F) * mult_;
                mult_ *= 128;
                len_bytes_++;
                if(b & 0x80){
                    if(len_bytes_ >= 4) return Result::Error;
                }else{
                    phase_ = Phase::Body;
                }
                break;
            }
            case Phase::Body: {
                if(got_ < remaining_){
                    size_t take = n - consumed;
                    if(take > remaining_ - got_) take = remaining_ - got_;
                    //溜め場所に入る分だけ写し、残りは読み捨てる
                    size_t keep = 0;
                    if(stored_ < cap_){
                        keep = cap_ - stored_;
                        if(keep > take) keep = take;
                        memcpy(buf_ + stored_, p + consumed, keep);
                        stored_ += keep;
                    }
                    consumed += take;
                    got_ += (uint32_t)take;
                }
                if(got_ == remaining_){
                    phase_ = Phase::Type;
                    return Result::Packet;
                }
                break;
            }
        }
    }
    return Result::NeedMore;
}
