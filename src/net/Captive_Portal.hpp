#pragma once

#include <cstddef>
#include <cstdint>

#include "task/Task.hpp"
#include "net/Http_Request.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// キャプティブポータル(ホテル・カフェ・駅等の「Wi-Fiには繋がるが、ログイン/同意のページを通すまで
// 外へ出られない」ネットワーク)の検出と、同意ページの送信。
//
//   CaptiveProbe … 決まったURL(既定 http://connectivitycheck.gstatic.com/generate_204)をGETし、
//                  204が返れば外へ出られる。別のページへ転送された/中身のあるページが返った = ポータル。
//                  繋がらない = インターネットへ出られない。HTTPだけなのでTLSの約40KBは使わない
//   CaptiveLogin … ポータルのページを取ってきて最初の<form>を読み、隠し項目・チェックボックス・
//                  送信ボタンの値をそのまま送る(「同意して接続」型のポータルはこれで通る)。
//                  利用者が入れる欄(部屋番号・メールアドレス・パスワード等)が空なら、送る前に
//                  呼び出し側(設定アプリ)がsetValue()で埋める
//
// このOSのブラウザはHTMLを描けない(Markdownだけ)ので、ポータルのページを見せる代わりにフォームを
// 直接読んで送る。JavaScriptで組み立てるポータルは通せない(その旨を表示する)。
//
// ページの中身は溜めずに流しながら読む(FormScanner。タグ1つぶん512Bの溜め場所だけ)。
// フォームの中身(Form)は約2.5KBで、ログインの間だけ new する(CaptiveLogin ごと)。
namespace CaptivePortal {

    constexpr const char* kDefaultProbeUrl = "http://connectivitycheck.gstatic.com/generate_204";

    enum class Verdict : uint8_t {
        Online,      // 外へ出られる
        Portal,      // ログイン/同意のページが要る
        NoInternet,  // 何も返ってこない(DNSが引けない・繋がらない)
    };

    // 判定の中身(ホストテスト用に切り出してある)。
    //   request_ok … 応答を最後まで読めたか / status … ステータスコード / body_bytes … 本文の大きさ
    //   location   … 3xxのLocation(相対でもよい。probe から見て解決する)
    // Portalなら out_portal へログインのページのURL(転送先。転送でなければプローブのURL自身)
    Verdict Judge(bool request_ok, int status, size_t body_bytes, const char* location,
                  const Url& probe, FixedString<PICO_STR_LL>& out_portal);

    // ---------------------------------------------------------------- フォーム
    enum class FieldKind : uint8_t {
        Hidden,    // 見えない値(セッションIDなど)。そのまま送る
        Text,      // 利用者が入れる欄(text/email/tel/number等)
        Password,
        Check,     // チェックボックス(同意の印。チェックして送る)・選ばれたラジオボタン
        Submit,    // 押されたボタンとして送る名前と値(最初の1つ)
    };

    struct Field {
        FieldKind kind = FieldKind::Hidden;
        FixedString<PICO_STR_M> name;
        FixedString<PICO_STR_L> value;
        FixedString<PICO_STR_M> label;   // 入力を頼むときに見せる名前(placeholder か name)
    };

    constexpr int kMaxFields = 12;

    struct Form {
        bool found = false;               // <form>が1つでもあったか
        bool post = false;                // method="post"
        FixedString<PICO_STR_LL> action;  // 空なら取ってきたページ自身へ送る
        Field fields[kMaxFields];
        int  count = 0;
        bool overflow = false;            // 項目がkMaxFieldsを超えた(超えた分は送らない)
        bool has_script = false;          // <script>があった(JavaScriptで動くポータルかもしれない)
        FixedString<PICO_STR_LL> refresh; // フォームが無いときの <meta http-equiv="refresh" content="0;url=...">

        // 利用者が埋めないと送れない欄(値が空のText/Password)の数
        int missingInputs() const;
        void clear();
    };

    // HTMLを流しながら読み、最初の<form>の中身を Form に詰める。<script>/<style>の中・コメントは飛ばす
    class FormScanner : public IHttpSink {
        public:
            explicit FormScanner(Form& out) : form_(out) {}
            bool write(const void* data, size_t len) override;
            void reset();
        private:
            enum class State : uint8_t { Text, Tag, Comment, Raw };
            void feed(char c);
            void endTag();
            Form& form_;
            State state_ = State::Text;
            char tag_[PICO_STR_512B];
            size_t tag_len_ = 0;
            bool tag_overflow_ = false;
            char quote_ = 0;
            uint8_t match_ = 0;       // Comment/Raw の終わりの印と何文字一致したか
            bool in_raw_style_ = false; // Raw が<style>か(<script>か)
            bool in_form_ = false;
            bool form_done_ = false;
    };

    // フォームの値を application/x-www-form-urlencoded にする。入りきらなければfalse
    bool BuildBody(const Form& form, FixedString<PICO_STR_1KiB>& out);
    // 送り先のURL(action を base から解決。GETなら本文をクエリにする)を作る
    bool BuildTarget(const Form& form, const Url& base, const FixedString<PICO_STR_1KiB>& body, Url& out);

    // ---------------------------------------------------------------- 判定
    // 1回の判定。呼び出し側が毎フレーム update() を呼び、PROCESSING 以外になったら verdict() を読んで delete する
    // (HttpRequest等と同じ流儀)
    class CaptiveProbe : public Task {
        public:
            bool begin(const char* probe_url);
            void update() override;
            Verdict verdict() const { return verdict_; }
            const FixedString<PICO_STR_LL>& portalUrl() const { return portal_; }
        private:
            class Counter : public IHttpSink {
                public:
                    size_t bytes = 0;
                    bool write(const void*, size_t len) override { bytes += len; return true; }
            };
            HttpRequest req_;
            Counter sink_;
            Url probe_;
            Verdict verdict_ = Verdict::NoInternet;
            FixedString<PICO_STR_LL> portal_;
    };

    // ---------------------------------------------------------------- ログイン
    class CaptiveLogin : public Task {
        public:
            enum class Phase : uint8_t {
                Fetching,    // ページを取ってきている
                NeedsInput,  // 利用者が入れる欄がある(setValue()で埋めて submit())
                Ready,       // そのまま送れる(submit())
                Submitting,  // 送っている
                Done,        // 送り終えた(外へ出られるかは呼び出し側がもう一度判定する)
                Failed,      // failText() に理由
            };

            bool begin(const char* portal_url);
            void update() override;
            Phase phase() const { return phase_; }
            const char* failText() const { return fail_.c_str(); }
            const Form& form() const { return form_; }

            // NeedsInput のとき: 埋める欄の i 番目(0始まり)。無ければ-1
            int missingIndex(int i) const;
            void setValue(int field_index, const char* value);
            bool submit();

        private:
            bool fetch(const Url& url);
            void fail(const char* text);
            HttpRequest req_;
            Form form_;
            FormScanner scanner_{form_};
            Url page_;
            int refresh_hops_ = 0;
            Phase phase_ = Phase::Fetching;
            FixedString<PICO_STR_1KiB> body_;
            FixedString<PICO_STR_L> fail_;
            class Discard : public IHttpSink {
                public:
                    bool write(const void*, size_t) override { return true; }
            } discard_;
    };
}
