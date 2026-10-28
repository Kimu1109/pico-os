#pragma once

#include <cstddef>
#include <cstdint>

// WebSocket(RFC 6455)の枠(フレーム)の読み書き。通信はしない(net/WebSocket_Client が使う)。
//
// 確保はしない。受け取った本文は呼び出し側が渡した固定長の溜め場所へ組み立てる
// (分割されたメッセージ(FIN=0 + 継続の枠)もここで1つにつなぐ)。制御の枠(ping/pong/close)は
// 125バイトまでと決まっているので、中に持つ。
namespace WsFrame {
    enum Opcode : uint8_t {
        Continuation = 0x0,
        Text = 0x1,
        Binary = 0x2,
        Close = 0x8,
        Ping = 0x9,
        Pong = 0xA,
    };

    // 閉じるときの理由の番号(RFC 6455 7.4.1。使うものだけ)
    constexpr uint16_t kCloseNormal = 1000;
    constexpr uint16_t kCloseGoingAway = 1001;
    constexpr uint16_t kCloseProtocol = 1002;
    constexpr uint16_t kCloseNoStatus = 1005;    // 番号の付いていないcloseを受けた(送ってはいけない)
    constexpr uint16_t kCloseAbnormal = 1006;    // closeを受けずに切れた(送ってはいけない)
    constexpr uint16_t kCloseTooBig = 1009;

    constexpr size_t kMaxHeaderBytes = 14;       // 2 + 長さ8 + マスク4
    constexpr size_t kMaxControlPayload = 125;
    constexpr size_t kKeyChars = 24;             // 16バイトの乱数をbase64にしたもの
    constexpr size_t kAcceptChars = 28;          // SHA-1(20バイト)をbase64にしたもの

    // Sec-WebSocket-Key(16バイトの乱数のbase64)。out は kKeyChars + 1 バイト以上(NUL終端する)
    void MakeKey(const uint8_t nonce[16], char* out);
    // key に対してサーバが返すべき Sec-WebSocket-Accept。out は kAcceptChars + 1 バイト以上(NUL終端する)
    void AcceptFor(const char* key, char* out);

    // 枠の頭を out(kMaxHeaderBytes 以上)へ書き、書いたバイト数を返す。
    // mask が非nullなら(クライアントは必ず)マスクの印と鍵を付ける
    size_t EncodeHeader(uint8_t* out, Opcode op, bool fin, uint64_t payload_len, const uint8_t* mask);
    // data をマスクの鍵でXORする(offset = 本文の先頭から何バイト目か。分けて送るとき用)
    void ApplyMask(uint8_t* data, size_t len, const uint8_t mask[4], size_t offset = 0);

    // 受け取ったバイト列から枠を読み、メッセージ・制御の枠がそろうたびに止まる
    class Parser {
        public:
            enum class Result : uint8_t {
                NeedMore,  // 渡した分を全部読んだが、まだそろっていない
                Message,   // メッセージ(text/binary)がそろった: data()/size()/binary()
                Ping,      // control()/controlSize()
                Pong,
                Close,     // closeCode()/control()(理由。先頭の番号2バイトは除いてある)
                Error,     // 規約違反か大きすぎる: errorCode()(閉じるときに送る番号)
            };

            // buf = メッセージを組み立てる溜め場所(非所有)。cap を超えるメッセージは Error(1009)
            void reset(uint8_t* buf, size_t cap);

            // p から最大 n バイト読む。consumed に読んだバイト数を返す。
            // NeedMore 以外を返したら、残り(n - consumed)は次の feed() へ渡し直すこと
            Result feed(const uint8_t* p, size_t n, size_t& consumed);

            const uint8_t* data() const { return buf_; }
            size_t size() const { return msg_len_; }
            bool binary() const { return msg_op_ == Binary; }
            const uint8_t* control() const { return ctrl_ + ctrl_skip_; }
            size_t controlSize() const { return ctrl_len_ - ctrl_skip_; }
            uint16_t closeCode() const { return close_code_; }
            uint16_t errorCode() const { return error_code_; }
            const char* errorText() const { return error_text_; }

        private:
            enum class Phase : uint8_t { Header, Payload };

            uint8_t* buf_ = nullptr;
            size_t cap_ = 0;
            size_t msg_len_ = 0;
            uint8_t msg_op_ = 0;         // 組み立て中のメッセージの種類(0 = 組み立て中でない)
            bool msg_done_ = false;      // 前回 Message を返した(次の枠で溜め場所を空にする)

            Phase phase_ = Phase::Header;
            uint8_t hdr_[kMaxHeaderBytes] = {};
            size_t hdr_len_ = 0;
            uint8_t op_ = 0;
            bool fin_ = false;
            uint64_t remain_ = 0;        // 今の枠の本文の残り

            uint8_t ctrl_[kMaxControlPayload] = {};
            size_t ctrl_len_ = 0;
            size_t ctrl_skip_ = 0;
            uint16_t close_code_ = 0;

            uint16_t error_code_ = 0;
            const char* error_text_ = "";

            size_t headerNeed() const;
            Result fail(uint16_t code, const char* text);
            Result beginFrame();
            Result endFrame();
    };
}
