#pragma once

#include <cstring>

#include "util/FixedString.hpp"

// スマホのカメラで読むと、そのWi-Fi(WPA)へ繋がるQRの文字列 "WIFI:T:WPA;S:<SSID>;P:<パスワード>;;" を作る。
// 値の中の \ ; , : " は前に \ を付ける。AP設定モード(WifiSetupScene)とWi-Fi転送の直接つなぐ(FileShareScene)が使う
namespace WifiQr {
    template <size_t N>
    void AppendEscaped(FixedString<N>& out, const char* s){
        for(; s && *s; s++){
            if(std::strchr("\;,:\"", *s)) out.append('\\');
            out.append(*s);
        }
    }

    template <size_t N>
    void Build(FixedString<N>& out, const char* ssid, const char* password){
        out.assign("WIFI:T:WPA;S:");
        AppendEscaped(out, ssid);
        out.append(";P:");
        AppendEscaped(out, password);
        out.append(";;");
    }
}
