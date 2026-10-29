#include "net/Udp_Socket.hpp"

#include <cstdio>

bool UdpSocket::begin(uint16_t local_port){
    close();
    if(udp_.begin(local_port) != 1) return false;
    open_ = true;
    return true;
}

void UdpSocket::close(){
    if(open_) udp_.stop();
    open_ = false;
}

bool UdpSocket::sendTo(const char* host, uint16_t port, const void* data, size_t len){
    if(!open_ || !host || !*host || port == 0 || len > kMaxDatagram) return false;
    if(udp_.beginPacket(host, port) != 1) return false;
    if(len > 0 && udp_.write((const uint8_t*)data, len) != len) return false;
    return udp_.endPacket() == 1;
}

int UdpSocket::receive(uint8_t* buf, size_t cap, char* from_addr, uint16_t& from_port){
    if(!open_) return -1;
    const int size = udp_.parsePacket();
    if(size <= 0) return -1;
    size_t n = (size_t)size;
    if(n > cap) n = cap;
    const int got = udp_.read(buf, n);
    //溜め場所に入らなかった残りは捨てる(次の parsePacket() が捨てる)
    const IPAddress ip = udp_.remoteIP();
    snprintf(from_addr, kMaxAddrText, "%u.%u.%u.%u",
             (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
    from_port = udp_.remotePort();
    return got < 0 ? 0 : got;
}
