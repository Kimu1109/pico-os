#pragma once

#include "net/Http_Get.hpp"
#include "net/Http_Response.hpp"
#include "util/Json_Reader.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>
#include <cstddef>

// 電卓の「変換」ページの通貨(為替)。レートを持ち、ネットワークから取り直し、SDへ控える係。
//
// - 取得元は既定で Frankfurter(https://frankfurter.dev、欧州中央銀行の参照レート。キー不要・応答は約600B)。
//   /sys/currency.cfg の `url = ...` で差し替えられる。open.er-api.com の形("base_code" / "rates")も読める
// - 応答のJSONは流しながら読む(JsonReader)。"rates" の中の「3文字の大文字 = 数」だけ拾う
// - **取得に失敗しても手元のレートは壊さない**。読み終えて1件以上取れたときだけ入れ替え、
//   /sys/currency.cfg へ書き直す(次の起動からはネットが無くても前回のレートで換算できる)
// - レートは「基準通貨1あたりいくつか」(base が EUR なら USD = 1.16)。換算は v / from × to
// - 円・ドル・ユーロ等のよく使う通貨を一覧の先頭へ並べる(kPreferred)
//
// 取得は使う人が[更新]を押したときだけ(自動では取りに行かない。通信と電池を使うため)。
class CurrencyRates : public IHttpSink, private JsonReader::Handler {
    public:
        static constexpr int kMaxRates = 48;
        static constexpr const char* kDefaultUrl = "https://api.frankfurter.dev/v1/latest";
        // 応答の上限(Frankfurterは約600B、open.er-apiは約3KB。これを超えたら壊れた応答とみなす)
        static constexpr size_t kMaxBodyBytes = 16 * 1024;

        struct Rate {
            char code[4];
            double per_base; // 基準通貨1あたり
        };

        enum class State : uint8_t { Idle, Fetching, Done, Failed };

        CurrencyRates() = default;
        CurrencyRates(const CurrencyRates&) = delete;
        CurrencyRates& operator=(const CurrencyRates&) = delete;
        ~CurrencyRates() override { cancel(); }

        int count() const { return count_; }
        bool hasRates() const { return count_ > 0; }
        const char* codeAt(int i) const { return (i >= 0 && i < count_) ? rates_[i].code : ""; }
        int find(const char* code) const;
        // 日本語の名前(知らない通貨はnullptr)
        static const char* NameOf(const char* code);
        // from番目の通貨の v を to番目の通貨へ
        bool convert(int from, int to, double v, double& out) const;

        // 取得したレートの日付("2026-10-21" 等。取得元が返したまま)と基準通貨
        const char* date() const { return date_.c_str(); }
        const char* base() const { return base_.c_str(); }
        const char* url() const { return url_.empty() ? kDefaultUrl : url_.c_str(); }

        // JSONを丸ごと読む(ホストテスト・取得の後始末と同じ道を通る)。1件以上読めたら今のレートを置き換えてtrue
        bool parse(const char* json, size_t len);

        // /sys/currency.cfg を読む(無ければ何もせずfalse)。url の指定もここで読む
        bool load(const char* path = nullptr);
        bool save(const char* path = nullptr) const;

        // 取得を始める。Wi-Fiに繋がっているかは呼び出し側が見ること。始められなければfalse(理由はmessage())
        bool beginFetch();
        // 毎フレーム呼ぶ。終わったら state() が Done/Failed になる(Doneならレートは入れ替わり、SDへ書いた後)
        void update();
        void cancel();

        State state() const { return state_; }
        bool busy() const { return state_ == State::Fetching; }
        // 最後の取得の結果(画面へそのまま出せる日本語)
        const char* message() const { return message_.c_str(); }
        // レートが入れ替わるたびに増える(画面の作り直しの目安)
        uint32_t revision() const { return revision_; }

        // IHttpSink
        bool write(const void* data, size_t len) override;

    private:
        Rate rates_[kMaxRates] = {};
        int count_ = 0;
        FixedString<PICO_STR_S> date_;
        FixedString<PICO_STR_S> base_;
        FixedString<PICO_STR_256B> url_;

        // 読みかけ(読み終えるまで今のレートには触らない)
        Rate staging_[kMaxRates] = {};
        int staging_count_ = 0;
        bool in_rates_ = false;
        FixedString<PICO_STR_S> staging_date_;
        FixedString<PICO_STR_S> staging_base_;
        size_t body_bytes_ = 0;
        JsonReader json_;

        HttpGet http_;
        State state_ = State::Idle;
        FixedString<PICO_STR_L> message_;
        uint32_t revision_ = 0;

        void beginParse();
        // 読み終えた中身を今のレートへ。取れたものが無ければfalse
        bool commitParse();
        // staging_ を「よく使う通貨が先、残りはアルファベット順」に並べ替える
        void sortStaging();
        static bool IsCode(const char* s);
        void stageRate(const char* code, double per_base);

        // JsonReader::Handler
        void onBegin(const char* key, bool is_array, int depth) override;
        void onEnd(bool is_array, int depth) override;
        void onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                     bool truncated, int depth) override;
};
