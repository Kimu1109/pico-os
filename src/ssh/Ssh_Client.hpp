#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"
#include "ssh/Ssh_Sha256.hpp"

#include "WiFi.h"

#include <cstddef>
#include <cstdint>

// SSH(バージョン2)のクライアント。対話シェル1本だけを扱う(ポート転送・SFTP等は無し)。
//
// 対応している方式は1つずつに絞ってある(どれも今のOpenSSHの既定で有効):
//   鍵交換   curve25519-sha256(@libssh.org も可)。kex-strict(Terrapin対策)にも対応
//   ホスト鍵 ssh-ed25519
//   暗号     chacha20-poly1305@openssh.com(MACはこれに含まれる)
//   認証     publickey(ssh-ed25519、OpenSSH形式の秘密鍵)/ password / keyboard-interactive
// 暗号の計算は lib/monocypher、SHA-256は Ssh_Sha256.hpp。
//
// ソケットを持つが、受信は update() から毎フレーム少しずつ進める(フレームを止めない)。
// 例外はTCPの接続(connect()の中で同期的に待つ。HttpTransportと同じ制約)と、
// 鍵交換の計算(X25519/Ed25519で数十ms程度の見込み。実機では未計測)。
//
// 利用者の判断が要るところでは止まって待つ:
//   HostKeyCheck … ホスト鍵を信頼するか(acceptHostKey())。known_hostsの照合は呼び出し側
//   NeedPassword … パスワード等の入力(providePassword())
//
// サーバから届いた端末への出力(と、このクライアント自身のお知らせ)は setOutput() の関数へ流す。
//
// 確保はしない(受信8KB + 送信1.3KB等の固定長。全体で約10KB)。使う間だけ new して持つこと。
class SshClient {
    public:
        enum class State : uint8_t {
            Idle,         // まだ繋いでいない
            Handshake,    // 鍵交換中
            HostKeyCheck, // ホスト鍵の確認待ち(acceptHostKey())
            Auth,         // 認証中
            NeedPassword, // 入力待ち(providePassword())
            Opening,      // シェルを開いている
            Open,         // シェルが動いている(send()できる)
            Closed,       // 終わった(理由は errorText())
        };

        using OutputFn = void (*)(void* ctx, const uint8_t* data, size_t len);

        static constexpr size_t kRxCap = 8192 + 64;
        static constexpr size_t kTxCap = 1280;
        static constexpr uint32_t kWindow = 16384;     // こちらの受信窓
        static constexpr uint32_t kMaxPacket = 4096;   // 1回に受け取るデータの上限
        static constexpr unsigned long kHandshakeTimeoutMs = 20000;
        static constexpr int kMaxPrompts = 4;

        SshClient() = default;
        ~SshClient();

        SshClient(const SshClient&) = delete;
        SshClient& operator=(const SshClient&) = delete;

        // 公開鍵認証に使う鍵(connect()の前に)。secretは種32+公開鍵32
        void setIdentity(const uint8_t secret[64], const uint8_t pub[32]);
        void setOutput(OutputFn fn, void* ctx){ out_fn_ = fn; out_ctx_ = ctx; }
        // 端末の大きさ。シェルが開いていれば window-change を送る
        void setTerminalSize(int cols, int rows, int width_px, int height_px);

        // TCPで繋いで挨拶を送る(同期)。失敗ならfalse(理由は errorText())
        bool connect(const char* host, uint16_t port, const char* user, unsigned long timeout_ms = 5000);
        void update();

        State state() const { return state_; }
        bool isOpen() const { return state_ == State::Open; }
        bool isActive() const { return state_ != State::Idle && state_ != State::Closed; }

        // HostKeyCheck中
        const uint8_t* hostKey() const { return host_pub_; }
        void acceptHostKey(bool accept);

        // NeedPassword中
        const char* promptText() const { return prompt_.c_str(); }
        bool promptEcho() const { return prompt_echo_; }
        void providePassword(const char* text);

        // シェルへ送る(キー入力)。Open以外ならfalse
        bool send(const uint8_t* data, size_t len);
        bool send(const char* s);

        // こちらから切る
        void disconnect();

        const char* errorText() const { return err_.c_str(); }
        // シェルの終了コード(届いていなければ-1)
        int exitStatus() const { return exit_status_; }
        const char* serverVersion() const { return v_s_.c_str(); }

    private:
        enum class AuthMethod : uint8_t { None, PublicKey, Password, Keyboard };

        WiFiClient sock_;
        State state_ = State::Idle;
        FixedString<PICO_STR_L> err_;
        unsigned long phase_start_ = 0;

        FixedString<PICO_STR_M> user_;
        bool have_identity_ = false;
        uint8_t id_secret_[64];
        uint8_t id_pub_[32];

        OutputFn out_fn_ = nullptr;
        void* out_ctx_ = nullptr;

        // ---- 受信 ----
        uint8_t rx_[kRxCap];
        size_t rx_len_ = 0;
        bool got_version_ = false;
        FixedString<PICO_STR_256B> v_s_;

        // ---- 送信 ----
        uint8_t tx_[kTxCap];
        size_t tx_pos_ = 0;
        bool tx_overflow_ = false;

        // ---- 暗号 ----
        struct CipherKeys {
            bool on = false;
            uint8_t main[32];
            uint8_t header[32];
        };
        CipherKeys tx_keys_, rx_keys_;
        uint8_t next_tx_[64];
        uint8_t next_rx_[64];
        uint32_t tx_seq_ = 0;
        uint32_t rx_seq_ = 0;

        // ---- 鍵交換 ----
        bool kex_in_progress_ = false;
        bool kexinit_sent_ = false;
        bool kex_reply_done_ = false;
        bool newkeys_sent_ = false;
        bool newkeys_recv_ = false;
        bool first_kex_done_ = false;
        bool strict_kex_ = false;
        uint8_t kexinit_c_[512];
        size_t kexinit_c_len_ = 0;
        Sha256 hash_;
        uint8_t eph_secret_[32];
        uint8_t eph_pub_[32];
        uint8_t session_id_[32];
        uint8_t host_pub_[32];
        bool have_host_pub_ = false;

        // ---- 認証 ----
        AuthMethod auth_method_ = AuthMethod::None;
        bool tried_pubkey_ = false;
        int password_attempts_ = 0;
        int keyboard_attempts_ = 0;
        FixedString<PICO_STR_LL> prompt_;
        bool prompt_echo_ = false;
        // keyboard-interactiveで1回に聞かれた問い(答えは揃ってからまとめて送る)
        int prompt_count_ = 0;
        int prompt_index_ = 0;
        FixedString<PICO_STR_LL> prompts_[kMaxPrompts];
        bool prompt_echos_[kMaxPrompts];
        FixedString<PICO_STR_L> answers_[kMaxPrompts];

        // ---- チャネル ----
        uint32_t remote_id_ = 0;
        uint32_t remote_window_ = 0;
        uint32_t remote_max_ = 0;
        uint32_t consumed_ = 0;
        int replies_expected_ = 0;
        int replies_seen_ = 0;
        bool close_sent_ = false;
        int exit_status_ = -1;
        int term_cols_ = 30, term_rows_ = 16, term_wpx_ = 240, term_hpx_ = 256;
        // 鍵の交換し直しの間や相手の窓が足りない間に溜めておく送信
        uint8_t pending_out_[512];
        size_t pending_len_ = 0;

        void fail(const char* text);
        void closeSocket();
        void notice(const char* text);
        void output(const uint8_t* data, size_t len);

        // パケットを組む
        void begin(uint8_t msg);
        void putByte(uint8_t v);
        void putU32(uint32_t v);
        void putBytes(const void* p, size_t n);
        void putString(const void* p, size_t n);
        void putCStr(const char* s);
        void putMpint(const uint8_t* be, size_t n);
        bool sendPacket();

        void pumpSocket();
        bool parseVersion();
        bool parsePacket();
        void handle(const uint8_t* p, size_t n, uint32_t seq);

        void sendKexInit();
        void onKexInit(const uint8_t* p, size_t n);
        void onKexReply(const uint8_t* p, size_t n);
        void sendNewKeys();
        void onNewKeys();
        void kexDone();
        void deriveKeys(const uint8_t* k_mpint, size_t k_len, const uint8_t h[32]);

        void onAuthFailure(const uint8_t* p, size_t n);
        void nextAuth(const char* methods);
        void sendAuthNone();
        void sendAuthPublicKey();
        void sendAuthPassword(const char* pw);
        void sendAuthKeyboard();
        void onInfoRequest(const uint8_t* p, size_t n);
        void askPrompt();

        void openChannel();
        void onChannelOpenConfirm(const uint8_t* p, size_t n);
        void onChannelReply(bool ok);
        void onChannelData(const uint8_t* data, size_t n);
        void sendWindowChange();
        void flushPending();
        bool sendChannelData(const uint8_t* data, size_t len);
};
