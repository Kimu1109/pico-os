#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/apps/TerminalView.hpp"
#include "gui/widgets/apps/TermKeyBar.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "ssh/Vt_Terminal.hpp"
#include "ssh/Ssh_Client.hpp"
#include "util/FixedString.hpp"

// SSHアプリ。画面全体が1つの端末で、接続先・ホスト鍵の確認・パスワードも端末の中で聞く
// (普通の ssh コマンドと同じ流れ。パスワードは画面に出さない)。
//
// キーボードはテキストエディタと同じく、端末の下に**据え置いて**直接打つ
// (KeyboardFunctions::Show(this, English, docked=true))。キーボードの入力欄は常に空にしておき、
// 打った文字が入るたびに(onDisplayChanged())すぐシェルへ送って空に戻す:
//   - 改行キー           … CR を送る
//   - 空のときの1文字削除 … DEL(0x7F)を送る(onBackspaceAtStart())
//   - 空のときの← →     … カーソルキーを送る(onCursorAtEdge())
//   - 日本語の変換中     … 読みは送らずに端末のカーソル位置へ重ねて出し、確定したら送る
// キーボードに無いキー(Esc/Tab/Ctrl/↑↓/^C)は端末の下の補助キーの列(TermKeyBar)で打つ。
// 物理キーボード(KeyInputFunctions)の打鍵はキー盤を通さず onKey() で直接シェルへ送る。
// 右上のボタンでキーボードを出し入れでき、出し入れのたびに端末の行数を変えてサーバへ知らせる。
//
// **踏み台(ProxyJump)**: 接続先を「user@host -J user@踏み台[:port]」と書くと、踏み台へSSHしてから
// その先の host:port への通り道(direct-tcpip)の上でもう一度SSHする(OpenSSHの ssh -J と同じ)。
// 外からTailscaleのtailnet上のホストへ繋ぐ用途を想定している(踏み台=Tailscaleの入った自宅の機械。
// hostはMagicDNSの名前でよい。名前は踏み台の側で引かれる)。暗号は相手のホストまで途切れない。
// ホスト鍵の確認とパスワードは、踏み台と相手の両方についてそれぞれ聞く。
//
// 設定は /sys/ssh.cfg(無くてよい): target = user@host[:port][ -J user@host[:port]](前回の接続先)、font = small | large。
// 公開鍵認証の鍵は /sys/ssh/id_ed25519(OpenSSH形式のssh-ed25519、パスフレーズ無し)があれば使う。
// 信頼したホスト鍵は /sys/ssh/known_hosts に追記する(22番以外は "[host]:port")。
//
// シーン本体は約30KB(VtTerminalの格子とスクロールバック)。SshClient(約10KB)は繋いでいる間だけ持つ。
class SshScene : public Scene, public ITextInputTarget {
    private:
        enum class Prompt : uint8_t { None, Target, HostKey, Secret };

        static constexpr int MARGIN = 2;
        static constexpr int kKeyBarH = 24;

        VtTerminal term;
        SshClient* client = nullptr;              // シェルを開く相手
        SshClient::State last_state = SshClient::State::Idle;
        SshClient* jump = nullptr;                // 踏み台(使うときだけ)
        SshClient::State last_jump_state = SshClient::State::Idle;
        SshTunnel* tunnel = nullptr;              // 踏み台の通り道(clientが話す相手)
        SshClient* prompt_client = nullptr;       // ホスト鍵/パスワードを聞いている相手

        Prompt prompt = Prompt::None;
        FixedString<PICO_STR_L> line;       // 端末の中で打っている1行(接続先/yes/パスワード)
        bool line_echo = true;
        FixedString<PICO_STR_L> target_default;
        FixedString<PICO_STR_M> host;
        FixedString<PICO_STR_M> user;
        uint16_t port = 22;
        bool use_jump = false;
        FixedString<PICO_STR_M> jump_host;
        FixedString<PICO_STR_M> jump_user;
        uint16_t jump_port = 22;
        bool small_font = false;
        bool started = false;

        // 接続(同期で画面が止まる)は「接続しています」を1回描いてから始める
        int connect_wait_frames = -1;

        bool syncing = false;
        int last_bottom = -1;
        bool last_small = false;

        Button* back_button = nullptr;
        Button* conn_button = nullptr;
        Button* font_button = nullptr;
        Button* kb_button = nullptr;
        TerminalView* view = nullptr;
        TermKeyBar* keybar = nullptr;

        void say(const char* s){ this->term.write(s); }
        static void OnOutput(void* ctx, const uint8_t* data, size_t len);

        void loadConfig();
        void layout();
        void refreshButtons();

        void startTargetPrompt();
        void startPrompt(Prompt p, const char* text, bool echo);
        void submitLine();
        bool parseTarget(const char* s);
        static bool ParseEndpoint(const char* s, size_t n, FixedString<PICO_STR_M>& user,
                                  FixedString<PICO_STR_M>& host, uint16_t& port);
        void beginConnect();
        void onStateChanged(SshClient* c, SshClient::State s);
        void onJumpOpen();
        void dropClient();
        bool isActive() const {
            return (this->client && this->client->isActive()) || (this->jump && this->jump->isActive());
        }
        SshClient* newClient();
        void hostOf(SshClient* c, const char*& host, uint16_t& port, const char*& user) const;

        // 入力(キーボード/補助キー)
        void inputText(const char* s, size_t n);
        void inputEnter();
        void inputBackspace();
        void inputArrow(char dir); // 'A'上 'B'下 'C'右 'D'左 'H'Home 'F'End
        void inputInterrupt();
        void sendToShell(const char* s, size_t n);
        void onKeyBar(TermKeyBar::Key key);

        void openKeyboard();

    public:
        SshScene() = default;
        ~SshScene() override;

        const char* getName() const override { return "SSH"; }
        // 離れると接続を切るので、ステータスバー/トーストのタップで別の画面へ移らない
        bool keepForeground() const override { return true; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

        // ---- ITextInputTarget ----
        void onShow(ITextInputWidget* keyboard) override;
        void onTextChanged(ITextInputWidget*) override {}
        void onHide(ITextInputWidget* keyboard) override;
        void onDisplayChanged(ITextInputWidget* keyboard) override;
        bool onBackspaceAtStart(ITextInputWidget* keyboard) override;
        bool onCursorAtEdge(ITextInputWidget* keyboard, int dir) override;
        // 物理キーボード: 打鍵をそのままシェルへ送る(Ctrl+文字・Alt=ESC前置・Esc/Tab/矢印/Home/End/Delete/PageUp/PageDown)。
        // 接続前(接続先・yes/no・パスワード)は端末の中の1行へ入れる
        bool onKey(const KeyInputFunctions::Event& ev) override;

        bool getIsSingleLine() override { return false; }
        void setIsSingleLine(bool) override {}
};
