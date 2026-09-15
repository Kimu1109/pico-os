#pragma once

// ---------------------------------------------------------------------------
// PCビルド / ホストテスト共用のTCPクライアント。
//
// pico-osのWi-Fi「状態」(WiFi.h)は母艦の設定を触らない偽物だが、
// **通信そのものは本物のソケットで行う**。母艦でサーバ(script/reference_server.py)を立てれば、実機へ
// 焼かずにプロトコルごと開発・デバッグできる。
//
// 実機(arduino-pico)のWiFiClientと同じ呼び出し方に揃えてあるので、src/側は
// PCと実機で同じコードのまま動く。
//
// SDにもLovyanGFXにも依存しないので、PCビルド(pc/compat/WiFi.h)と
// ホストテスト(script/host_test/stubs/WiFi.h)の両方から同じ実装を使える。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

class WiFiClientPC {
public:
    WiFiClientPC() = default;
    ~WiFiClientPC(){ stop(); }

    //コピーすると同じfdを二重にcloseするので禁止する
    WiFiClientPC(const WiFiClientPC&) = delete;
    WiFiClientPC& operator=(const WiFiClientPC&) = delete;

    void setTimeout(unsigned long ms){ timeout_ms_ = ms; }

    // 実機と同じく成功で1、失敗で0を返す。
    // connect(2)を非ブロッキングで開始しselect()で待つことで、到達しない相手でも
    // timeout_ms_ で必ず戻る(実機側の接続も同程度で切り上がる想定)
    int connect(const char* host, uint16_t port){
        stop();
        if(!host || !*host) return 0;

        char portStr[8];
        snprintf(portStr, sizeof(portStr), "%u", (unsigned)port);

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* list = nullptr;
        if(::getaddrinfo(host, portStr, &hints, &list) != 0 || !list) return 0;

        int fd = -1;
        for(addrinfo* it = list; it; it = it->ai_next){
            fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
            if(fd < 0) continue;

            const int flags = ::fcntl(fd, F_GETFL, 0);
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

            if(::connect(fd, it->ai_addr, it->ai_addrlen) == 0) break; //即時に繋がった

            if(errno == EINPROGRESS && waitWritable(fd)){
                int err = 0;
                socklen_t len = sizeof(err);
                if(::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) break;
            }

            ::close(fd);
            fd = -1;
        }
        ::freeaddrinfo(list);

        if(fd < 0) return 0;
        fd_ = fd;
        return 1;
    }

    bool connected(){
        if(fd_ < 0) return false;
        //相手が閉じていても、まだ読めるデータが残っていれば「接続中」として扱う
        //(実機のWiFiClientと同じ振る舞い。取りこぼしを防ぐため)
        if(available() > 0) return true;

        char probe;
        const ssize_t n = ::recv(fd_, &probe, 1, MSG_PEEK | MSG_DONTWAIT);
        if(n > 0) return true;
        if(n == 0) return false; //相手が閉じた
        return (errno == EAGAIN || errno == EWOULDBLOCK);
    }

    int available(){
        if(fd_ < 0) return 0;
        int count = 0;
        if(::ioctl(fd_, FIONREAD, &count) != 0) return 0;
        return count;
    }

    int read(uint8_t* buf, size_t size){
        if(fd_ < 0 || !buf || size == 0) return 0;
        const ssize_t n = ::recv(fd_, buf, size, MSG_DONTWAIT);
        if(n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
        return (int)n;
    }

    size_t write(const uint8_t* buf, size_t size){
        if(fd_ < 0 || !buf) return 0;

        size_t sent = 0;
        while(sent < size){
            const ssize_t n = ::send(fd_, buf + sent, size - sent, MSG_NOSIGNAL);
            if(n > 0){ sent += (size_t)n; continue; }
            if(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)){
                if(!waitWritable(fd_)) break;
                continue;
            }
            break;
        }
        return sent;
    }
    size_t write(const char* s){ return s ? write((const uint8_t*)s, strlen(s)) : 0; }
    size_t print(const char* s){ return write(s); }

    void stop(){
        if(fd_ >= 0){
            ::close(fd_);
            fd_ = -1;
        }
    }

    operator bool() const { return fd_ >= 0; }

private:
    bool waitWritable(int fd){
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);

        timeval tv;
        tv.tv_sec = (time_t)(timeout_ms_ / 1000);
        tv.tv_usec = (suseconds_t)((timeout_ms_ % 1000) * 1000);

        return ::select(fd + 1, nullptr, &set, nullptr, &tv) > 0;
    }

    int fd_ = -1;
    unsigned long timeout_ms_ = 3000;
};

using WiFiClient = WiFiClientPC;
