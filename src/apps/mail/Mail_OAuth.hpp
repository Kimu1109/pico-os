#pragma once

#include "net/Http_Request.hpp"
#include "util/Json_Reader.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// メールの Microsoft アカウント(Outlook.com / Hotmail / outlook.jp 等)のサインイン係。ImapClient が値メンバとして持つ。
//
// Outlook.com の IMAP は2024年からパスワード(LOGIN)を受け付けず、OAuth2 のアクセストークンを
// AUTHENTICATE XOAUTH2 で渡すしかない。本体ではブラウザを開けないので **デバイスコードの流れ**
// (RFC 8628)でサインインする:
//   1. devicecode へ client_id と scope を送る → 「https://microsoft.com/devicelogin で XXXX-XXXX を入れて」
//   2. 利用者がスマホ/PCのブラウザでコードを入れてサインインする間、token を interval 秒ごとに問い合わせる
//   3. 通ればアクセストークン(約1時間)と更新用トークン(refresh token)が返る
// 以後は更新用トークンからアクセストークンを取り直す(grant_type=refresh_token)。返った新しい更新用トークンは
// 保存し直す(Microsoft は更新のたびに新しいものを返す)。
//
// - client_id は利用者が Microsoft Entra(Azure)で登録したアプリのもの(mail.cfg の oauth-client-id)。
//   「個人の Microsoft アカウント」を許し、「パブリック クライアント フローを許可」を有効にしておく
//   (docs/design/apps-online.md「メール」)。client secret は使わない(端末に秘密は置けない)
// - トークンの応答は流しながら読む(JsonReader)。アクセストークンは1〜3KBあるので、その値だけ
//   JsonReader::Handler::stringBuffer() で置き場を差し出して丸ごと受け取る
// - 更新用トークンは /sys/mail_token.cfg に PICO_Secret で暗号化して置く(1行255Bずつに分けて、行ごとに用途の文字列を変える。
//   設定ファイルの値(160B)には入らない長さなので mail.cfg とは別のファイル)
// - 同時に投げる要求は1本だけ。接続は使い回さない(終わったらTLSの約40KBを返す)
//
// **約13KBある**(アクセストークン4KB・更新用トークン2KB×2(今のものと受信中)・デバイスコード1KB・送るボディ4KB)。
class MailOAuth : public IHttpSink, private JsonReader::Handler {
    public:
        constexpr static size_t kAccessTokenCap = PICO_STR_4KiB;
        constexpr static size_t kRefreshTokenCap = PICO_STR_2KiB;
        constexpr static size_t kDeviceCodeCap = PICO_STR_1KiB;
        constexpr static size_t kBodyCap = PICO_STR_4KiB;
        // 期限の少し前に古いとみなす(繋いでいる途中で切れないように)
        constexpr static uint32_t kExpiryMarginSec = 300;
        static constexpr const char* kDefaultAuthority = "https://login.microsoftonline.com";
        // 個人の Microsoft アカウント(Outlook.com 等)。職場/学校のアカウントも使うなら "common" か テナントのID
        static constexpr const char* kDefaultTenant = "consumers";
        static constexpr const char* kScope = "https://outlook.office.com/IMAP.AccessAsUser.All offline_access";
        // Outlook.com の IMAP サーバ
        static constexpr const char* kImapHost = "outlook.office365.com";

        struct Config {
            FixedString<PICO_STR_M> client_id;
            FixedString<PICO_STR_M> tenant;
            // "https://login.microsoftonline.com"(テストでは偽物のサーバを指す)
            FixedString<PICO_STR_L> authority;
        };

        enum class Phase : uint8_t {
            Idle,
            RequestCode,  // devicecode を問い合わせている
            WaitUser,     // 利用者がブラウザでコードを入れるのを待っている(token を間を空けて問い合わせる)
            Refresh,      // 更新用トークンからアクセストークンを取り直している
        };

        // 最後に終わった操作の結果
        enum class Result : uint8_t {
            None,
            Ok,          // アクセストークンを取れた
            NeedSignIn,  // 更新用トークンが無い/もう使えない(サインインし直す)
            Denied,      // サインインを断られた・コードの期限が切れた
            Failed,      // 通信の失敗等(しばらくして取り直してよい)
        };

        MailOAuth() = default;
        MailOAuth(const MailOAuth&) = delete;
        MailOAuth& operator=(const MailOAuth&) = delete;

        // 設定を変える。前の設定と違えばトークンも捨てる
        void configure(const Config& c);
        const Config& config() const { return cfg_; }

        // 更新用トークン(保存したもの)を持たせる/取り出す
        bool setRefreshToken(const char* token);
        bool hasRefreshToken() const { return refresh_len_ > 0; }
        const char* refreshToken() const { return refresh_; }
        // 更新用トークンが新しくなった(保存し直す)ごとに増える
        uint32_t refreshRevision() const { return refresh_rev_; }
        // トークンを全部捨てる(サインアウト・アカウントの切り替え)
        void forget();

        // 使えるアクセストークンがあるか(期限の kExpiryMarginSec 前まで)
        bool hasAccessToken() const;
        const char* accessToken() const { return access_; }
        size_t accessTokenLength() const { return access_len_; }
        // アクセストークンを捨てる(サーバに断られたとき。次は取り直す)
        void dropAccessToken();
        // アクセストークンを取ってから経った時間(ms)
        uint32_t accessTokenAgeMs() const;

        // サインインを始める(devicecode)。進行中の要求はやめる
        bool startSignIn();
        // 更新用トークンからアクセストークンを取り直す
        bool startRefresh();
        void cancel();
        void update();
        bool busy() const { return phase_ != Phase::Idle; }
        Phase phase() const { return phase_; }

        // ---- サインインの案内(WaitUser の間) ----
        const char* userCode() const { return user_code_.c_str(); }
        const char* verificationUri() const { return verification_uri_.c_str(); }
        // コードの期限までの残り(秒)
        uint32_t secondsLeft() const;

        // ---- 結果 ----
        Result result() const { return result_; }
        const char* errorText() const { return error_text_.c_str(); }
        // 操作が終わる/案内が変わるたびに増える
        uint32_t revision() const { return rev_; }

        // ---- 更新用トークンの保存(/sys/mail_token.cfg) ----
        // 読めたらtrue。needs_rewrite は「平文か古い鍵で書かれている(書き直すべき)」
        static bool LoadRefreshToken(char* out, size_t cap, bool* needs_rewrite = nullptr);
        static bool SaveRefreshToken(const char* token);
        static void ClearRefreshToken();

        // application/x-www-form-urlencoded の値として書き足す(RFC 3986 の unreserved 以外は %XX)
        static bool AppendFormValue(FixedString<kBodyCap>& out, const char* value);

        // IHttpSink
        bool write(const void* data, size_t len) override;

    private:
        Config cfg_;
        HttpRequest req_;
        JsonReader json_;
        bool json_ok_ = true;
        FixedString<kBodyCap> body_;

        Phase phase_ = Phase::Idle;
        bool in_flight_ = false;   // 要求を投げて応答を待っている(WaitUser では問い合わせの合間がある)
        Result result_ = Result::None;
        FixedString<PICO_STR_LL> error_text_;
        uint32_t rev_ = 0;

        // ---- サインイン ----
        char device_code_[kDeviceCodeCap] = {};
        FixedString<PICO_STR_S> user_code_;
        FixedString<PICO_STR_L> verification_uri_;
        uint32_t code_started_ms_ = 0;
        uint32_t code_expires_sec_ = 0;
        uint32_t interval_sec_ = 5;
        uint32_t next_poll_ms_ = 0;

        // ---- トークン ----
        char access_[kAccessTokenCap] = {};
        size_t access_len_ = 0;
        uint32_t access_got_ms_ = 0;
        uint32_t access_expires_sec_ = 0;
        char refresh_[kRefreshTokenCap] = {};
        size_t refresh_len_ = 0;
        uint32_t refresh_rev_ = 0;

        // ---- 応答の読みかけ ----
        // アクセストークンは access_ へ直に読む(取りに行くのは今のものが使えないときだけなので)。
        // 更新用トークンは今のものを壊さないよう、受信用の置き場へ読んでから写す
        bool access_truncated_ = false;
        char new_refresh_[kRefreshTokenCap] = {};
        size_t new_refresh_len_ = 0;
        bool new_refresh_truncated_ = false;
        uint32_t got_expires_in_ = 0;
        uint32_t got_interval_ = 0;
        FixedString<PICO_STR_M> got_error_;
        FixedString<PICO_STR_LL> got_error_desc_;

        // 送るボディは body_(要求が終わるまで持っておく)
        bool post(const char* path);
        void beginTokenPoll();
        void finish();
        void finishWith(Result r, const char* why);
        void resetParsed();

        // JsonReader::Handler
        char* stringBuffer(const char* key, int depth, size_t& cap) override;
        void onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                     bool truncated, int depth) override;
};
