#include "net/Ws_Frame.hpp"

#include "crypto/Sha1.hpp"
#include "util/Base64.hpp"

#include <cstring>
#include "system/I18n_Functions.hpp"

namespace {
    // RFC 6455 1.3 で決まっている固定の文字列
    constexpr const char* kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
}

void WsFrame::MakeKey(const uint8_t nonce[16], char* out){
    const size_t n = Base64::Encode(nonce, 16, out);
    out[n] = '\0';
}

void WsFrame::AcceptFor(const char* key, char* out){
    Sha1 sha;
    sha.update(key, strlen(key));
    sha.update(kGuid, strlen(kGuid));
    uint8_t digest[Sha1::kDigestBytes];
    sha.finish(digest);
    const size_t n = Base64::Encode(digest, sizeof(digest), out);
    out[n] = '\0';
}

size_t WsFrame::EncodeHeader(uint8_t* out, Opcode op, bool fin, uint64_t payload_len, const uint8_t* mask){
    size_t n = 0;
    out[n++] = (uint8_t)((fin ? 0x80 : 0x00) | (op & 0x0F));
    const uint8_t mbit = mask ? 0x80 : 0x00;
    if(payload_len < 126){
        out[n++] = (uint8_t)(mbit | payload_len);
    }else if(payload_len <= 0xFFFF){
        out[n++] = (uint8_t)(mbit | 126);
        out[n++] = (uint8_t)(payload_len >> 8);
        out[n++] = (uint8_t)payload_len;
    }else{
        out[n++] = (uint8_t)(mbit | 127);
        for(int i = 7; i >= 0; i--) out[n++] = (uint8_t)(payload_len >> (8 * i));
    }
    if(mask){
        memcpy(out + n, mask, 4);
        n += 4;
    }
    return n;
}

void WsFrame::ApplyMask(uint8_t* data, size_t len, const uint8_t mask[4], size_t offset){
    for(size_t i = 0; i < len; i++) data[i] ^= mask[(offset + i) & 3];
}

void WsFrame::Parser::reset(uint8_t* buf, size_t cap){
    *this = Parser{};
    buf_ = buf;
    cap_ = cap;
}

WsFrame::Parser::Result WsFrame::Parser::fail(uint16_t code, const char* text){
    error_code_ = code;
    error_text_ = text;
    return Result::Error;
}

// 頭をそろえるのに要るバイト数(1バイト目と2バイト目を見るまでは2)
size_t WsFrame::Parser::headerNeed() const {
    if(hdr_len_ < 2) return 2;
    size_t need = 2;
    const uint8_t len7 = hdr_[1] & 0x7F;
    if(len7 == 126) need += 2;
    else if(len7 == 127) need += 8;
    if(hdr_[1] & 0x80) need += 4;
    return need;
}

// 頭がそろった: 枠の種類と長さを確かめる
WsFrame::Parser::Result WsFrame::Parser::beginFrame(){
    fin_ = (hdr_[0] & 0x80) != 0;
    op_ = hdr_[0] & 0x0F;
    if(hdr_[0] & 0x70) return fail(kCloseProtocol, TR("拡張の印(RSV)が立った枠を受けた"));
    //サーバからの枠はマスクしてはいけない(RFC 6455 5.1)
    if(hdr_[1] & 0x80) return fail(kCloseProtocol, TR("マスクされた枠を受けた"));

    const uint8_t len7 = hdr_[1] & 0x7F;
    uint64_t len = len7;
    if(len7 == 126){
        len = ((uint64_t)hdr_[2] << 8) | hdr_[3];
    }else if(len7 == 127){
        len = 0;
        for(int i = 0; i < 8; i++) len = (len << 8) | hdr_[2 + i];
    }
    remain_ = len;

    if(msg_done_){
        //前回メッセージを渡し終えた。次のメッセージは空から組み立てる
        msg_len_ = 0;
        msg_op_ = 0;
        msg_done_ = false;
    }

    const bool control = (op_ & 0x08) != 0;
    if(control){
        if(op_ != Close && op_ != Ping && op_ != Pong) return fail(kCloseProtocol, TR("知らない制御の枠を受けた"));
        if(!fin_ || len > kMaxControlPayload) return fail(kCloseProtocol, TR("制御の枠が分割されている/長すぎる"));
        ctrl_len_ = 0;
        ctrl_skip_ = 0;
    }else if(op_ == Continuation){
        if(msg_op_ == 0) return fail(kCloseProtocol, TR("始まりの無い継続の枠を受けた"));
    }else if(op_ == Text || op_ == Binary){
        if(msg_op_ != 0) return fail(kCloseProtocol, TR("前のメッセージが終わる前に次が始まった"));
        msg_op_ = op_;
        msg_len_ = 0;
    }else{
        return fail(kCloseProtocol, TR("知らない種類の枠を受けた"));
    }
    if(!control && msg_len_ + len > cap_) return fail(kCloseTooBig, TR("メッセージが大きすぎる"));
    phase_ = Phase::Payload;
    return Result::NeedMore;
}

// 本文まで読み終えた枠を片付ける
WsFrame::Parser::Result WsFrame::Parser::endFrame(){
    phase_ = Phase::Header;
    hdr_len_ = 0;
    switch(op_){
        case Ping: return Result::Ping;
        case Pong: return Result::Pong;
        case Close:
            if(ctrl_len_ == 1) return fail(kCloseProtocol, TR("closeの番号が欠けている"));
            if(ctrl_len_ >= 2){
                close_code_ = (uint16_t)((ctrl_[0] << 8) | ctrl_[1]);
                ctrl_skip_ = 2;
            }else{
                close_code_ = kCloseNoStatus;
            }
            return Result::Close;
        default:
            if(!fin_) return Result::NeedMore; //続きの枠を待つ
            msg_done_ = true;
            return Result::Message;
    }
}

WsFrame::Parser::Result WsFrame::Parser::feed(const uint8_t* p, size_t n, size_t& consumed){
    consumed = 0;
    while(consumed < n || (phase_ == Phase::Payload && remain_ == 0)){
        if(phase_ == Phase::Header){
            const size_t need = headerNeed();
            while(hdr_len_ < need && consumed < n){
                hdr_[hdr_len_++] = p[consumed++];
            }
            if(hdr_len_ < headerNeed()) continue; //2バイト目で必要な長さが伸びた/まだ足りない
            const Result r = beginFrame();
            if(r != Result::NeedMore) return r;
            continue;
        }
        //本文
        if(remain_ > 0){
            size_t take = n - consumed;
            if((uint64_t)take > remain_) take = (size_t)remain_;
            if(op_ & 0x08){
                memcpy(ctrl_ + ctrl_len_, p + consumed, take);
                ctrl_len_ += take;
            }else{
                memcpy(buf_ + msg_len_, p + consumed, take);
                msg_len_ += take;
            }
            consumed += take;
            remain_ -= take;
        }
        if(remain_ == 0){
            const Result r = endFrame();
            if(r != Result::NeedMore) return r;
        }
    }
    return Result::NeedMore;
}
