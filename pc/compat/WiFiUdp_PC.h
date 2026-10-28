#pragma once

// ---------------------------------------------------------------------------
// PCビルド / ホストテスト共用のUDP(実機の arduino-pico の WiFiUDP と同じ呼び方)。
//
// WiFiClient_PC.h と同じく、**通信そのものは本物のソケットで行う**(母艦のLANの機器とも話せる)。
// 使う分(begin / beginPacket / write / endPacket / parsePacket / read / remoteIP / remotePort / stop)だけ。
// Webビルドではソケットが無いので、送っても届かない(呼び方だけ通る)。
// ---------------------------------------------------------------------------

#include "WiFiClient_PC.h" // IPAddress

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

class WiFiUDPPC {
public:
    static constexpr size_t kMaxPacket = 1500;

    WiFiUDPPC() = default;
    ~WiFiUDPPC(){ stop(); }
    WiFiUDPPC(const WiFiUDPPC&) = delete;
    WiFiUDPPC& operator=(const WiFiUDPPC&) = delete;

    // 実機と同じく成功で1。port=0 なら空いている番号を使う
    uint8_t begin(uint16_t port){
        stop();
        if(!open()) return 0;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if(::bind(fd_, (sockaddr*)&addr, sizeof(addr)) != 0){
            stop();
            return 0;
        }
        return 1;
    }

    void stop(){
        if(fd_ >= 0) ::close(fd_);
        fd_ = -1;
        out_len_ = in_len_ = in_pos_ = 0;
    }

    uint16_t localPort() const {
        if(fd_ < 0) return 0;
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        if(::getsockname(fd_, (sockaddr*)&addr, &len) != 0) return 0;
        return ntohs(addr.sin_port);
    }

    int beginPacket(const char* host, uint16_t port){
        if(!host || !*host) return 0;
        if(fd_ < 0 && !open()) return 0;
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* list = nullptr;
        if(::getaddrinfo(host, nullptr, &hints, &list) != 0 || !list) return 0;
        memcpy(&dest_, list->ai_addr, sizeof(dest_));
        ::freeaddrinfo(list);
        dest_.sin_port = htons(port);
        out_len_ = 0;
        return 1;
    }
    int beginPacket(const IPAddress& ip, uint16_t port){ return beginPacket(ip.c_str(), port); }

    size_t write(const uint8_t* buf, size_t size){
        if(!buf) return 0;
        if(size > kMaxPacket - out_len_) size = kMaxPacket - out_len_;
        memcpy(out_ + out_len_, buf, size);
        out_len_ += size;
        return size;
    }
    size_t write(uint8_t b){ return write(&b, 1); }

    int endPacket(){
        if(fd_ < 0) return 0;
        const ssize_t n = ::sendto(fd_, out_, out_len_, 0, (sockaddr*)&dest_, sizeof(dest_));
        out_len_ = 0;
        return n >= 0 ? 1 : 0;
    }

    // 届いていれば1つ取り込み、その大きさを返す(無ければ0)
    int parsePacket(){
        in_len_ = in_pos_ = 0;
        if(fd_ < 0) return 0;
        sockaddr_in from{};
        socklen_t flen = sizeof(from);
        const ssize_t n = ::recvfrom(fd_, in_, sizeof(in_), MSG_DONTWAIT, (sockaddr*)&from, &flen);
        if(n <= 0) return 0;
        in_len_ = (size_t)n;
        char text[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
        snprintf(remote_ip_.text_, sizeof(remote_ip_.text_), "%s", text);
        remote_port_ = ntohs(from.sin_port);
        return (int)n;
    }

    int available() const { return (int)(in_len_ - in_pos_); }

    int read(unsigned char* buf, size_t len){
        const size_t left = in_len_ - in_pos_;
        if(len > left) len = left;
        memcpy(buf, in_ + in_pos_, len);
        in_pos_ += len;
        return (int)len;
    }
    int read(char* buf, size_t len){ return read((unsigned char*)buf, len); }

    IPAddress remoteIP() const { return remote_ip_; }
    uint16_t remotePort() const { return remote_port_; }

private:
    int fd_ = -1;
    sockaddr_in dest_{};
    uint8_t out_[kMaxPacket];
    size_t out_len_ = 0;
    uint8_t in_[kMaxPacket];
    size_t in_len_ = 0;
    size_t in_pos_ = 0;
    IPAddress remote_ip_;
    uint16_t remote_port_ = 0;

    bool open(){
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if(fd_ < 0) return false;
        ::fcntl(fd_, F_SETFL, ::fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
        const int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
        return true;
    }
};

using WiFiUDP = WiFiUDPPC;
