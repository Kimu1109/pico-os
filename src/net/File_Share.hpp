#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "util/FixedString.hpp"
#include "consts.hpp"

// Wi-Fi越しのファイル転送(services/FileShare_Functions が使う)の、ソケットに依存しない部分。
// 同じLANのブラウザ・curlから、SDのファイルを一覧・ダウンロード・アップロード・削除・名前の変更・フォルダ作成する。
//
//   GET  /                    … ページ(net/File_Share_Page)。キーは要らない(中身を持たない)。
//                               ?k=<キー> はページのJSがクッキーへ移し、URLから消す(QRで開くとき)
//   GET  /api/info            … {"name":"<本体の名前>"}(キーの確かめにも使う)
//   GET  /api/ls?p=<フォルダ>  … 1行1つ「名前\t種類(f/d)\t大きさ」(TSV)
//   GET  /f/<パス>            … ダウンロード(?dl=1 で保存のダイアログ)
//   PUT  /f/<パス>            … アップロード。本文がファイルの中身そのもの(Content-Length が要る)
//   POST /api/mkdir?p= / rm?p= / mv?p=&to=
//
// 守り(docs/design/file-share.md):
//   - キー: 画面に出す8文字。クッキー pk か見出し X-Pico-Key で渡す。書き換える要求(PUT/POST)は
//     X-Pico-Key が必須(独自の見出しはCORSの事前確認になり、本体が答えないので他のサイトから送れない)
//   - Host が本体のIP・<名前>.local・<名前> 以外なら断る(DNSリバインディング)
//   - パスは %XX を戻してから正規化し、名前ごとに PICO_IO::isValidName()。/sys は見せず触らせない
namespace FileShare {

    constexpr size_t kMaxHead = 2048;   // 要求の見出し(本文の前)の上限
    constexpr size_t kKeyLen = 8;

    enum class Method : uint8_t { Get, Head, Put, Post, Other };

    // 要求の見出しを溜めて読む。本文は溜めない(呼び出し側がそのままSDへ流す)
    class RequestHead {
        public:
            enum class State : uint8_t { Reading, Done, TooLarge, Bad };
            // data を食わせる。Done になったら used に「見出しの分として使ったバイト数」を返す
            // (data の残り = 本文の頭)。Reading のうちは used == len
            State feed(const uint8_t* data, size_t len, size_t& used);
            void reset();

            State state() const { return state_; }
            Method method() const { return method_; }
            const char* path() const { return path_.c_str(); }    // ? より前(%XXのまま)
            const char* query() const { return query_.c_str(); }  // ? より後(無ければ "")
            const char* host() const { return host_.c_str(); }
            const char* cookie() const { return cookie_.c_str(); }
            const char* key() const { return key_.c_str(); }      // X-Pico-Key
            bool hasLength() const { return has_length_; }
            uint64_t contentLength() const { return content_length_; }

        private:
            bool parse();
            char buf_[kMaxHead + 1];
            size_t len_ = 0;
            Method method_ = Method::Other;
            FixedString<PICO_STR_1KiB> path_;
            FixedString<PICO_STR_1KiB> query_;
            FixedString<PICO_STR_L> host_;
            FixedString<PICO_STR_256B> cookie_;
            FixedString<PICO_STR_S> key_;
            uint64_t content_length_ = 0;
            bool has_length_ = false;
            State state_ = State::Reading;
    };

    // ---- 何の要求か ----
    enum class Route : uint8_t {
        Page, Info, List, Download, Upload, Mkdir, Remove, Move,
        NotFound,          // 知らない場所
        MethodNotAllowed,  // 場所はあるが方法が違う
    };
    // path は %XX のまま。Download/Upload のとき file_part に "/f" より後(%XXのまま)を返す
    Route Classify(Method m, const char* path, const char** file_part);
    // 書き換える要求か(X-Pico-Key が要る)
    inline bool IsMutating(Route r){
        return r == Route::Upload || r == Route::Mkdir || r == Route::Remove || r == Route::Move;
    }

    // ---- 文字列の道具 ----
    // %XX と(query なら)+ を戻して out へ。壊れた %、NUL、out に入らないときは false
    bool PercentDecode(const char* s, size_t len, bool plus_is_space, char* out, size_t cap);
    // query("a=1&b=2")から key の値を戻して out へ。無ければ false
    bool QueryValue(const char* query, const char* key, char* out, size_t cap);
    // Cookie 見出しから name の値を out へ。無ければ false
    bool CookieValue(const char* cookie, const char* name, char* out, size_t cap);
    // RFC 3986 の非予約文字以外を %XX にして足す(Content-Disposition の filename*)
    template <size_t N>
    void AppendPercentEncoded(FixedString<N>& out, const char* s){
        static const char kHex[] = "0123456789ABCDEF";
        for(; s && *s; s++){
            const unsigned char c = (unsigned char)*s;
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                               c == '-' || c == '.' || c == '_' || c == '~';
            if(plain){ out.append((char)c); continue; }
            out.append('%');
            out.append(kHex[c >> 4]);
            out.append(kHex[c & 15]);
        }
    }
    // JSONの文字列の中身として足す(" \ と制御文字)
    template <size_t N>
    void AppendJsonEscaped(FixedString<N>& out, const char* s){
        for(; s && *s; s++){
            const unsigned char c = (unsigned char)*s;
            if(c == '"' || c == '\\'){ out.append('\\'); out.append((char)c); }
            else if(c < 0x20) out.appendFormat("\\u%04x", c);
            else out.append((char)c);
        }
    }

    // ---- パス ----
    enum class PathCheck : uint8_t { Ok, Bad, Forbidden };
    // URLの中のパス(%XXのまま、len バイト)を戻して CheckPath() する
    PathCheck PathFromUrl(const char* raw, size_t len, FixedString<PICO_PATH_LEN>& out);
    // 戻し済みのパスを正規化して確かめる("/" 始まりにする)。名前ごとに isValidName()、
    // /sys とその中(大文字小文字を問わない)は Forbidden
    PathCheck CheckPath(const char* decoded, FixedString<PICO_PATH_LEN>& out);
    // 正規化済みのパスが /sys かその中か(FATは大文字小文字を区別しない)
    bool IsSystemPath(const char* path);

    // ---- 守り ----
    // Host("picoos.local:80" 等)が本体を指すか。ip は "192.168.1.23"(空なら比べない)
    bool HostAllowed(const char* host, const char* hostname, const char* ip, uint16_t port);
    // 長さを漏らさずに比べる
    bool KeyEquals(const char* a, const char* b);
    // キーに使う字(紛らわしい 0 1 l o i を抜いた31字)
    constexpr const char kKeyAlphabet[] = "23456789abcdefghjkmnpqrstuvwxyz";
    // 乱数 kKeyLen バイトからキーを作る
    void MakeKey(const uint8_t* rnd, char (&out)[kKeyLen + 1]);

    // 間違ったキーの連続の数を数え、超えたらしばらく全部断る(総当たりを遅くする)
    class Throttle {
        public:
            constexpr static int kMaxFailures = 5;
            constexpr static uint32_t kLockMs = 30000;
            bool blocked(uint32_t now_ms) const { return locked_ && now_ms - since_ < kLockMs; }
            void fail(uint32_t now_ms){
                if(blocked(now_ms)) return;
                locked_ = false;
                if(++failures_ >= kMaxFailures){ locked_ = true; since_ = now_ms; failures_ = 0; }
            }
            void success(){ failures_ = 0; }
        private:
            int failures_ = 0;
            uint32_t since_ = 0;
            bool locked_ = false;
    };

    // ---- 応答 ----
    // 拡張子から Content-Type
    const char* ContentType(const char* name);
    // ステータス行と見出し。length < 0 なら Content-Length を付けない(閉じて終わり)。
    // extra は "Name: value\r\n" を並べたもの(nullptr可)。Connection: close と Cache-Control: no-store を付ける
    void FormatHeader(int status, const char* content_type, int64_t length, const char* extra, char* out, size_t cap);
    const char* StatusText(int status);
    // 一覧の1行("名前\tf\t大きさ\n")を足す。入りきらなければ何も足さずに false
    template <size_t N>
    bool AppendListLine(FixedString<N>& out, const char* name, bool is_dir, uint64_t size){
        char tail[32];
        const int tl = snprintf(tail, sizeof(tail), "\t%c\t%llu\n", is_dir ? 'd' : 'f',
                                (unsigned long long)(is_dir ? 0 : size));
        if(out.length() + strlen(name) + (size_t)tl > out.capacity()) return false;
        out.append(name);
        out.append(tail);
        return true;
    }
    // ページ本体(net/File_Share_Page.cpp)
    extern const char kPageHtml[];
    size_t PageHtmlLength();

}
