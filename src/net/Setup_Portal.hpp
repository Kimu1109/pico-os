#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// スマホからWi-Fiを設定するための小さなWebページ(AP設定モード。services/WifiSetup_Functions が使う)。
//
// 本体が自分でアクセスポイント(AP)になり、スマホをそこへ繋ぐと、このページで
// 「繋ぎたいWi-FiのSSIDとパスワード」を入れて本体へ送れる。オンスクリーンキーボードで
// 長いパスワードを打つ手間を省くのが目的。
//
//   GET  (どのパスでも) … 入力のページ。スマホのポータル検出(/generate_204・/hotspot-detect.html等)にも
//                          このページを返すので、APへ繋ぐとスマホが自動でページを開く
//   POST /save          … ssid / pass を受け取り、呼び出し側の保存(SaveFn)の結果をページで返す
//
// ここは受け取ったバイト列を溜めて読むのと、返すページを作るだけ(ソケットにもWi-Fiにも依存しない)。
//
// ページの言語はスマホの Accept-Language で決める(ja で始まれば日本語、ほかの言語は英語、無ければ日本語)。
// 本体の言語の設定は見ない(スマホを持っている人の言語に合わせる。docs/design/i18n.md「Wi-Fi転送のページ」と同じ考え)。
// 1つの要求は kMaxRequest バイトまで(超えたら413)。本文は application/x-www-form-urlencoded だけ読む。
namespace SetupPortal {

    constexpr size_t kMaxRequest = 2048;

    // 要求を溜めて読む。feed()が Done/Error を返したら method()/path()/body() を読む
    class Request {
        public:
            enum class State : uint8_t { Reading, Done, TooLarge, Bad };
            State feed(const void* data, size_t len);
            void reset();
            State state() const { return state_; }
            bool isPost() const { return post_; }
            const char* path() const { return path_.c_str(); }
            const char* body() const { return buf_ + body_start_; }
            // Accept-Language が ja で始まる・無い(既定)ならtrue。それ以外の言語なら英語のページにする
            bool japanese() const { return japanese_; }
        private:
            bool parseHeaders();
            char buf_[kMaxRequest + 1];
            size_t len_ = 0;
            size_t body_start_ = 0;
            size_t content_length_ = 0;
            bool headers_done_ = false;
            bool post_ = false;
            bool japanese_ = true;
            FixedString<PICO_STR_M> path_;
            State state_ = State::Reading;
    };

    // application/x-www-form-urlencoded の key を探して値を(+と%XXを戻して)out へ。無ければfalse
    bool FormValue(const char* body, const char* key, char* out, size_t cap);

    // HTMLへ埋める文字列の & < > " ' を文字参照にして足す
    template <size_t N>
    void AppendEscaped(FixedString<N>& out, const char* s){
        for(; s && *s; s++){
            switch(*s){
                case '&':  out.append("&amp;");  break;
                case '<':  out.append("&lt;");   break;
                case '>':  out.append("&gt;");   break;
                case '"':  out.append("&quot;"); break;
                case '\'': out.append("&#39;");  break;
                default:   out.append(*s);       break;
            }
        }
    }

    enum class SaveResult : uint8_t { Saved, Full, Invalid };
    // 送られたSSIDとパスワードを保存する(呼び出し側。WifiProfiles::Put 等)
    using SaveFn = SaveResult (*)(const char* ssid, const char* password);

    // 応答の本文(HTML)を作り、ステータスコードを返す。本文は約1.5KBで、Response(2KiB)に収まる。
    // saved_ssid には保存できたときだけSSIDを入れる(呼び出し側が接続へ進むため)。
    // device_name はページの見出しに出す本体の名前。本文は大きいので呼び出し側が静的/確保した所に持つ
    using Response = FixedString<PICO_STR_2KiB>;
    int Respond(const Request& req, SaveFn save, const char* device_name,
                Response& body, FixedString<PICO_STR_M>& saved_ssid);
    // ステータス行とヘッダ(Content-Length: body_len、Connection: close)。180バイトあれば足りる
    void FormatHeader(int status, size_t body_len, char* out, size_t cap);

    // ---- どの名前を聞かれても本体のアドレスを答えるDNS(スマホがページを自動で開くため) ----
    // 問い合わせ(UDPの中身)への答えを out へ書き、長さを返す。答えないもの(応答・問い合わせ以外・
    // 質問が1つでない・壊れている)は0。Aレコード(IN)の質問には ip(上位バイトが先頭)を1つ答え、
    // それ以外の型(AAAA等)には答えの無い成功を返す(スマホがIPv4で聞き直す)
    constexpr size_t kMaxDnsPacket = 512;
    size_t BuildDnsReply(const uint8_t* query, size_t len, uint32_t ip, uint8_t* out, size_t cap);
}
