#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// IPv4アドレスとホスト名の文字列の読み書き(固定IP/DNSの設定・mDNSのホスト名用)。
//
// アドレスは uint32_t の「上位バイトが先頭のオクテット」(192.168.1.2 = 0xC0A80102)で持つ。
// lwIPのip4_addr_t(ネットワークバイト順)やArduinoのIPAddressへは、使う側(NetworkFunctions)が
// 1オクテットずつ取り出して渡す(Octet())。ここはWi-Fiにも描画にも依存しないので、ホストテストで動かせる。
namespace NetAddr {

    inline uint8_t Octet(uint32_t a, int i){ return (uint8_t)(a >> (24 - 8 * i)); }
    inline uint32_t Make(uint8_t a, uint8_t b, uint8_t c, uint8_t d){
        return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    }

    // "192.168.1.2" を読む。前後の空白は許す。各オクテットは0〜255の10進(先頭の0は3桁まで許す)。
    // end には読み終えた位置を返す(nullptrなら末尾まで全部がアドレスでなければ失敗)
    inline bool Parse(const char* s, uint32_t& out, const char** end = nullptr){
        if(!s) return false;
        while(*s == ' ' || *s == '\t') s++;
        uint32_t v = 0;
        for(int i = 0; i < 4; i++){
            if(*s < '0' || *s > '9') return false;
            int n = 0, digits = 0;
            while(*s >= '0' && *s <= '9'){
                n = n * 10 + (*s - '0');
                if(++digits > 3 || n > 255) return false;
                s++;
            }
            v = (v << 8) | (uint32_t)n;
            if(i < 3){
                if(*s != '.') return false;
                s++;
            }
        }
        if(end){
            *end = s;
        }else{
            while(*s == ' ' || *s == '\t') s++;
            if(*s != '\0') return false;
        }
        out = v;
        return true;
    }

    // "a.b.c.d" を書く(16バイトあれば足りる)
    inline void Format(uint32_t a, char* out, size_t cap){
        if(!out || cap == 0) return;
        snprintf(out, cap, "%u.%u.%u.%u", Octet(a, 0), Octet(a, 1), Octet(a, 2), Octet(a, 3));
    }

    // プレフィックス長(0〜32)⇔サブネットマスク
    inline uint32_t PrefixToMask(int prefix){
        if(prefix <= 0) return 0;
        if(prefix >= 32) return 0xFFFFFFFFu;
        return 0xFFFFFFFFu << (32 - prefix);
    }
    // 1が上から連続していないマスク(255.0.255.0 等)は-1
    inline int MaskToPrefix(uint32_t mask){
        int n = 0;
        while(n < 32 && (mask & (0x80000000u >> n))) n++;
        return (PrefixToMask(n) == mask) ? n : -1;
    }

    // "192.168.1.50/24" か "192.168.1.50/255.255.255.0" を読む。"/..." を省くと /24 とみなす
    // (家庭のLANのほとんどがこれなので、打つ手間を省く)
    inline bool ParseCidr(const char* s, uint32_t& ip, uint32_t& mask){
        const char* p = nullptr;
        uint32_t a = 0;
        if(!Parse(s, a, &p)) return false;
        while(*p == ' ' || *p == '\t') p++;
        uint32_t m = PrefixToMask(24);
        if(*p == '/'){
            p++;
            const char* q = p;
            int n = 0, digits = 0;
            while(*q >= '0' && *q <= '9'){ n = n * 10 + (*q - '0'); q++; digits++; }
            if(digits > 0 && digits <= 2 && *q != '.'){
                if(n < 1 || n > 32) return false;
                m = PrefixToMask(n);
                p = q;
            }else{
                if(!Parse(p, m, &p)) return false;
                if(MaskToPrefix(m) <= 0) return false;
            }
            while(*p == ' ' || *p == '\t') p++;
        }
        if(*p != '\0') return false;
        ip = a;
        mask = m;
        return true;
    }

    // 区切り(カンマ・空白)で並んだアドレスを最大 max 個読む。空文字列は0個で成功。
    // max を超える/読めないものがあれば失敗
    inline bool ParseList(const char* s, uint32_t* out, int max, int& count){
        count = 0;
        if(!s) return true;
        const char* p = s;
        while(true){
            while(*p == ' ' || *p == '\t' || *p == ',') p++;
            if(*p == '\0') return true;
            if(count >= max) return false;
            uint32_t a = 0;
            if(!Parse(p, a, &p)) return false;
            if(*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') return false;
            out[count++] = a;
        }
    }

    // 端末に付けられるアドレスか(0.0.0.0・ループバック・マルチキャスト以上は不可)
    inline bool IsUsableHost(uint32_t a){
        const uint8_t first = Octet(a, 0);
        return first != 0 && first != 127 && first < 224;
    }

    // ip/mask の組として正しいか(ネットワーク部・ブロードキャストのアドレスは不可。/31・/32 は例外的に許す)
    inline bool IsValidHostInSubnet(uint32_t ip, uint32_t mask){
        if(!IsUsableHost(ip)) return false;
        const int prefix = MaskToPrefix(mask);
        if(prefix <= 0) return false;
        if(prefix >= 31) return true;
        const uint32_t host = ip & ~mask;
        return host != 0 && host != ~mask;
    }

    inline bool SameSubnet(uint32_t a, uint32_t b, uint32_t mask){ return (a & mask) == (b & mask); }

    // mDNS/DHCPのホスト名(1ラベル): 英数字とハイフン、1〜63文字、先頭と末尾はハイフン不可。
    // 大文字は受け付けるが、保存する側が小文字へ揃える(ToLowerHostname)
    constexpr size_t kMaxHostnameLen = 63;
    inline bool IsValidHostname(const char* s){
        if(!s) return false;
        const size_t n = strlen(s);
        if(n == 0 || n > kMaxHostnameLen) return false;
        if(s[0] == '-' || s[n - 1] == '-') return false;
        for(size_t i = 0; i < n; i++){
            const char c = s[i];
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
            if(!ok) return false;
        }
        return true;
    }
    inline void ToLowerHostname(char* s){
        for(; s && *s; s++) if(*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
    }
}
