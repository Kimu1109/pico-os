#pragma once

#include "WiFi.h"
#include "WiFiUdp.h"

#include <cstddef>
#include <cstdint>

// UDPの送り受け(実機は arduino-pico の WiFiUDP、PCは pc/compat/WiFiUdp_PC.h の本物のソケット)。
// Luaの pico.socket.udp が使う。IoT機器(WLED・Tasmota等)への短い命令や、LANの中の発見(ブロードキャスト)向け。
//
// - 送るときの名前解決は同期的(実機は WiFi.hostByName。IPの数字で書けば引かない)
// - 1つのデータグラムは kMaxDatagram バイトまで(Wi-FiのMTUに収まる大きさ。分割はしない)
// - 受けたものは1回の receive() で1つ取り出す。溜め場所を超えた分は捨てる(UDPの約束どおり)
class UdpSocket {
    public:
        static constexpr size_t kMaxDatagram = 1472;
        // 送り元のIPの文字列("192.168.1.10" / IPv6も入る長さ)
        static constexpr size_t kMaxAddrText = 48;

        UdpSocket() = default;
        ~UdpSocket(){ close(); }
        UdpSocket(const UdpSocket&) = delete;
        UdpSocket& operator=(const UdpSocket&) = delete;

        // local_port = 0 なら空いている番号で待つ(返事だけ受けたいとき)
        bool begin(uint16_t local_port = 0);
        void close();
        bool isOpen() const { return open_; }

        bool sendTo(const char* host, uint16_t port, const void* data, size_t len);

        // 届いていれば1つ取り出して長さを返す(無ければ -1)。from_addr は kMaxAddrText バイト以上
        int receive(uint8_t* buf, size_t cap, char* from_addr, uint16_t& from_port);

    private:
        WiFiUDP udp_;
        bool open_ = false;
};
