#pragma once

#include <cstddef>
#include <cstdint>

#include "firmware/Fw_Image.hpp"
#include "firmware/Fw_Store.hpp"
#include "ssh/Ssh_Sha256.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 更新ファイルを受け取る元(SDのファイル・PCからのTCP接続)
class FwSource {
    public:
        virtual ~FwSource() = default;
        // 読めたバイト数。0 = 今は無い(次のフレームでまた呼ぶ)、-1 = 終わり/切れた/失敗
        virtual int read(uint8_t* p, size_t n) = 0;
        // 相手へ1行知らせる(TCPのときだけ意味がある。改行は付けて送る)
        virtual void reply(const char* line){ (void)line; }
        // 相手がいなくなったか(確認を待っている間に見る)
        virtual bool closed(){ return false; }
};

// 1回の更新の流れ(見出し → 確認 → 中身を置き場へ → 読み直して確かめる → ローダーへの指示)。
// 描画・SD・ソケット・時計に依存しない(受け取り元と置き場は差し込む)ので、ホストテストで全部の経路を確かめられる。
// OSへのつなぎ(SD・TCP・電池・再起動・通知)は services/Firmware_Functions。
//
// TCPの取り決め(script/firmware_tool.py push と揃える):
//   PC → 見出し+署名(192バイト)
//   本体 → "OK" / "WAIT <理由>"(本体で確認中。続けて "OK" か "ERR ...") / "ERR <理由>"
//   PC → "OK" を受け取ってから中身
//   本体 → "DONE"(この後すぐ再起動する) / "ERR <理由>"
class FwUpdate {
    public:
        enum class State : uint8_t {
            Header,   // 見出しを待っている
            Confirm,  // 利用者の確認を待っている
            Payload,  // 中身を置き場へ書いている
            Verify,   // 置き場から読み直して確かめている
            Done,     // 指示を書いた(再起動すれば書き換わる)
            Failed,
        };

        struct Env {
            uint32_t current_serial = 0;
            const char* current_git = "";
            bool allow_unsigned = false; // 署名を確かめられない更新ファイルも(確認のうえで)受け付ける
            bool always_confirm = false; // 署名済みで新しい版でも確認する(SDから選んだとき)
            const uint8_t (*keys)[32] = nullptr; // nullptrなら焼き込みの鍵(Fw_Keys.hpp)
            int key_count = 0;
        };

        static constexpr size_t kBufBytes = 4096;

        // src と store は終わるまで生きていること
        void start(FwSource* src, FwStore* store, const Env& env);
        // 毎フレーム。最大 budget_bytes ぶん進める
        void step(size_t budget_bytes);
        // Confirm のときの答え
        void confirm(bool yes);
        // やめる(置き場を片付ける。Done の後なら指示も消す)
        void cancel(const char* reason = "取り消しました");

        State state() const { return state_; }
        bool finished() const { return state_ == State::Done || state_ == State::Failed; }
        const char* message() const { return message_.c_str(); }
        const FwImage::Header& header() const { return header_; }
        FwImage::Sig signature() const { return sig_; }
        // 確認の理由(改行区切り。Confirm のとき)
        const char* confirmText() const { return confirm_.c_str(); }
        bool isDowngrade() const { return header_.serial < env_.current_serial; }
        uint32_t progressDone() const { return state_ == State::Verify ? verified_ : received_; }
        uint32_t progressTotal() const { return header_.payload_bytes; }

    private:
        void fail(const char* text);
        void stepHeader();
        void stepPayload(size_t budget);
        void stepVerify(size_t budget);
        void beginPayload();

        FwSource* src_ = nullptr;
        FwStore* store_ = nullptr;
        Env env_{};
        State state_ = State::Failed;
        FixedString<PICO_STR_LL> message_;
        FixedString<PICO_STR_LL> confirm_;
        FwImage::Header header_{};
        FwImage::Sig sig_ = FwImage::Sig::Unsigned;
        uint8_t prefix_[FwImage::kPrefixBytes] = {};
        size_t prefix_len_ = 0;
        uint32_t received_ = 0;
        uint32_t verified_ = 0;
        bool writing_ = false;
        bool reading_ = false;
        bool mounted_ = false;
        uint8_t gz_head_[3] = {};
        uint8_t gz_tail_[4] = {};
        Sha256 sha_;
        uint8_t buf_[kBufBytes];
};
