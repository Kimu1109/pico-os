#pragma once

#include "apps/mail/Imap_Proto.hpp"
#include "apps/mail/Mail_Mime.hpp"
#include "net/Http_Transport.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// メール(IMAP)の受信係。MailScene が値メンバとして持ち、毎フレーム update() を呼ぶ。
//
// - 繋ぎ方は IMAPS(ポート993、最初からTLS)。TLSは net/HttpTransport を借りる(証明書の確かめ方も同じ)。
//   `tls = false` で平文のIMAP(143)にもできるが、パスワードがそのまま流れるので自前のサーバの試験用。
//   STARTTLS(平文で繋いでから暗号化に切り替える)は使えない
// - 認証は LOGIN(ユーザー名とパスワード)だけ。Gmail はアプリパスワード、Outlook.com は
//   OAuth しか受け付けないので使えない(docs/design/apps-online.md「メール」)
// - 設定は /sys/mail.cfg(host / port / tls / user / password / mailbox / mark-read)。
//   パスワードは PICO_Secret で暗号化して置く(平文で書かれていたら、読んだときに暗号化して書き直す)
// - 一覧は新しい方から kMaxMessages 通(FETCH で UID・既読・日時・大きさ・From/Subject/Date だけ)。
//   取り終えてから入れ替えるので、途中で失敗しても前の一覧は残る
// - 本文は1通ずつ、先頭の kBodyFetchBytes だけ取る(BODY[]<0.n>)。届いた分から MailMime::TextExtractor に
//   流して文字だけを取り出すので、メール全体はRAMに載せない。既読にするか(mark-read)は設定で選ぶ
// - **接続は使い回す**。一覧を開いている間は kPollMs ごとに NOOP で新着(EXISTS)を見て、増えていれば取り直す。
//   失敗したら5秒→10秒→…→2分と間を空ける。ログインの失敗(NO)は自動では繰り返さない
//
// 受信は update() から少しずつ進める(フレームを止めない)。例外は接続(TLSのハンドシェイク込みで
// connect() の中で同期的に待つ。HttpTransport と同じ制約)。
//
// **約30KBある**(一覧 kMaxMessages 通 × 2(表示中と受信中)+ 本文 kBodyCap + 応答の組み立て4KB +
// TextExtractor 約3KB)。さらに繋いでいる間はTLSの約40KBを持つ。
class ImapClient : private Imap::Reader::Handler {
    public:
        constexpr static int kMaxMessages = 20;
        constexpr static size_t kBodyCap = 12 * 1024;
        constexpr static uint32_t kBodyFetchBytes = 32 * 1024;
        constexpr static uint32_t kPollMs = 3 * 60 * 1000;
        constexpr static uint32_t kMinBackoffMs = 5000;
        constexpr static uint32_t kMaxBackoffMs = 120000;
        constexpr static uint32_t kResponseTimeoutMs = 30000;
        constexpr static unsigned long kConnectTimeoutMs = 15000;
        constexpr static uint16_t kDefaultPort = 993;

        enum class State : uint8_t {
            NotConfigured, // /sys/mail.cfg が無い/足りない
            Offline,       // Wi-Fiに繋がっていない
            Ok,            // 最後の問い合わせは成功した
            Error,         // 最後の問い合わせは失敗した(しばらくして取り直す)
            AuthError,     // ログインできない(取り直さない)
        };

        struct Config {
            FixedString<PICO_STR_M> host;
            uint16_t port = kDefaultPort;
            bool tls = true;
            FixedString<PICO_STR_L> user;
            FixedString<PICO_STR_L> password;
            FixedString<PICO_STR_M> mailbox;
            bool mark_read = true;
        };

        ImapClient() = default;
        ImapClient(const ImapClient&) = delete;
        ImapClient& operator=(const ImapClient&) = delete;

        // /sys/mail.cfg を読む。使える設定が揃っていればtrue
        bool loadConfig();
        // 設定を直接与える(ホストテスト用。loadConfig() と同じ検査をする)
        bool configure(const Config& c);
        // サーバ・ユーザー名・パスワードを /sys/mail.cfg へ書く(設定画面から)。password が空なら今のまま
        static bool SaveAccount(const char* host, uint16_t port, const char* user, const char* password);
        // 今の設定("host:port" と user。画面の入力欄の初期値)
        const Config& config() const { return cfg_; }

        // 待たずに今すぐ一覧を取り直す(「更新」ボタン。ログインの失敗の後もこれで再開する)
        void refreshNow();
        // 本文を取る。前の本文の取得は取り消す
        void openMessage(uint32_t uid);
        // 本文の表示をやめた(取得中なら結果を捨てる)
        void closeMessage();

        // 毎フレーム呼ぶ。network_up=false(Wi-Fiが切れている)の間は何もせず接続も閉じる
        void update(bool network_up = true);
        // 進行中の問い合わせをやめ、接続も閉じる(onExit用)
        void stop();
        // 通信中か(画面をスリープさせない目安)
        bool busy() const { return cmd_ != Cmd::None || phase_ == Phase::Greeting; }

        State state() const { return state_; }
        const char* statusText() const { return status_.c_str(); }
        uint32_t statusRevision() const { return status_rev_; }

        // ---- 一覧(新しい順)。取り直すたびに listRevision() が増える ----
        int messageCount() const { return count_; }
        const Imap::Summary& messageAt(int i) const { return list_[i]; }
        const Imap::Summary* findMessage(uint32_t uid) const;
        uint32_t listRevision() const { return list_rev_; }
        bool loaded() const { return loaded_; }
        // メールボックスの全通数(一覧に出しているのはそのうち新しい kMaxMessages 通)
        uint32_t totalCount() const { return exists_; }
        int unseenCount() const;

        // ---- 本文 ----
        // 取れた本文のUID(0なら無し)。取り直すたびに bodyRevision() が増える
        uint32_t bodyUid() const { return body_uid_; }
        uint32_t bodyRevision() const { return body_rev_; }
        bool bodyLoading() const { return want_body_ != 0 || cmd_ == Cmd::Body; }
        bool bodyOk() const { return body_ok_; }
        const char* bodyError() const { return body_err_.c_str(); }
        const char* bodyText() const { return body_; }
        size_t bodyLength() const { return body_len_; }
        // 長すぎて途中までしか取っていない/置き場に入りきらなかった
        bool bodyTruncated() const { return body_truncated_; }
        bool bodyFromHtml() const { return body_html_; }
        // 本文と一緒に読んだヘッダー(一覧より詳しい: 宛先・CC・添付)
        const MailMime::TextExtractor& bodyInfo() const { return mime_; }

        // 接続を持っているか(使い回しの確認用)
        bool connected() const { return phase_ != Phase::Disconnected; }

    private:
        enum class Phase : uint8_t { Disconnected, Greeting, Login, Select, Ready };
        enum class Cmd : uint8_t { None, Login, Select, Noop, List, Body, Logout };

        Config cfg_;
        bool configured_ = false;

        HttpTransport net_;
        Imap::Reader reader_;
        Phase phase_ = Phase::Disconnected;
        Cmd cmd_ = Cmd::None;
        uint32_t tag_ = 0;
        uint32_t cmd_tag_ = 0;
        uint32_t cmd_started_ms_ = 0;
        bool closing_ = false;

        // ---- 状態 ----
        State state_ = State::NotConfigured;
        FixedString<PICO_STR_LL> status_;
        uint32_t status_rev_ = 0;
        uint32_t next_poll_ms_ = 0;
        uint32_t backoff_ms_ = 0;
        bool wait_manual_ = false;
        bool want_list_ = false;  // 一覧を取り直す(NOOPの後で必ず)
        bool want_poll_ = false;  // NOOP で新着を見る(増えていれば取り直す)

        // ---- メールボックス ----
        uint32_t exists_ = 0;
        uint32_t listed_exists_ = 0xFFFFFFFFu;
        uint32_t uidvalidity_ = 0;

        // ---- 一覧 ----
        Imap::Summary list_[kMaxMessages];
        int count_ = 0;
        uint32_t list_rev_ = 0;
        bool loaded_ = false;
        // 受信中(FETCH が終わったら list_ と入れ替える)
        Imap::Summary new_list_[kMaxMessages];
        int new_count_ = 0;
        uint32_t fetch_lo_ = 0;

        // ---- 本文 ----
        uint32_t want_body_ = 0;
        uint32_t body_req_uid_ = 0;
        uint32_t body_uid_ = 0;
        uint32_t body_rev_ = 0;
        bool body_ok_ = false;
        bool body_got_ = false;    // 応答に本文のリテラルがあった
        bool body_seen_ = false;   // 応答の FLAGS に \Seen があった
        bool body_has_flags_ = false;
        bool body_html_ = false;
        bool body_truncated_ = false;
        uint32_t body_literal_size_ = 0;
        FixedString<PICO_STR_LL> body_err_;
        char body_[kBodyCap];
        size_t body_len_ = 0;
        MailMime::TextExtractor mime_;
        bool streaming_body_ = false;

        bool connectNow();
        void disconnect();
        bool send(Cmd cmd, const char* args);
        void startNext();
        void handleTagged(const Imap::Response& r);
        void handleUntagged(const Imap::Response& r, const char* text, size_t len);
        void finishBody(bool ok, const char* why);
        void setStatus(State s, const char* text);
        void failLater(const char* why);

        // Imap::Reader::Handler
        bool onLiteralStart(const char* line, size_t len, uint32_t size) override;
        void onLiteralData(const uint8_t* data, size_t n) override;
        void onLiteralEnd() override;
        void onResponse(const char* text, size_t len, bool overflow) override;
};
