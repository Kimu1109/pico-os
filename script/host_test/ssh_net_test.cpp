// SSHクライアント(src/ssh/Ssh_Client)の結合テスト。本物の OpenSSH の sshd を相手にする。
//
// run_net.sh が使い捨てのホスト鍵・利用者の鍵・設定で sshd を 127.0.0.1 に立ててから呼ぶ。
// 引数: <port> <利用者の秘密鍵(OpenSSH形式)のパス> <ユーザー名> <ホスト鍵の公開鍵ファイル(.pub)>
//
// 確かめること:
//   - 鍵交換(curve25519-sha256 + kex-strict) → ホスト鍵の確認で止まる → 信頼すると先へ進む
//   - ホスト鍵の指紋が ssh-keygen -l と同じ書き方になる / known_hosts の照合と追記
//   - 公開鍵認証(ssh-ed25519) → pty付きのシェル → コマンドの出力が端末エミュレータに出る
//   - window-change(端末の大きさの変更)が stty size に反映される
//   - 大きな出力(受信窓の調整を何度も跨ぐ)を取りこぼさない
//   - exit で終了コードと共に閉じる
//   - 踏み台(ProxyJump): 同じsshdを踏み台にして、direct-tcpipの通り道の上でもう一度SSHする。
//     名前(localhost)は踏み台の側で引かれる / 大きな出力が通り道の受信窓(8KB)を跨いでも欠けない /
//     踏み台から繋げない相手は理由付きで失敗する / 中のSSHを閉じると踏み台も閉じる
//   - ホスト鍵を信頼しなければ中止される / 鍵が無くパスワードも使えないサーバでは認証に失敗する
#include "ssh/Ssh_Client.hpp"
#include "ssh/Ssh_Util.hpp"
#include "ssh/Vt_Terminal.hpp"
#include "OS_Data.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

static int failures = 0;
#define CHECK(cond) do { \
    if(!(cond)){ printf("  [NG] %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } \
    else { printf("  [OK] %s\n", #cond); } \
} while(0)

static std::string g_out;
static VtTerminal g_term;

static void OnOutput(void*, const uint8_t* data, size_t len){
    g_out.append((const char*)data, len);
    g_term.write(data, len);
}

static std::string ReadFile(const char* path){
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 状態が変わる(か出力に needle が出る)まで回す
static bool Pump(SshClient& c, bool (*until)(SshClient&), int timeout_ms = 10000){
    for(int i = 0; i < timeout_ms / 5; i++){
        c.update();
        if(until(c)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static const char* g_needle = "";
static bool OutputHasNeedle(SshClient& c){
    return g_out.find(g_needle) != std::string::npos || c.state() == SshClient::State::Closed;
}
static bool WaitFor(SshClient& c, const char* needle, int timeout_ms = 10000){
    g_needle = needle;
    Pump(c, OutputHasNeedle, timeout_ms);
    return g_out.find(needle) != std::string::npos;
}

static std::string TermRowText(int y){
    std::string s;
    const VtTerminal::Cell* row = g_term.row(y);
    for(int x = 0; x < g_term.cols(); x++){
        const uint16_t ch = row[x].ch;
        s.push_back(ch == 0 ? ' ' : (ch < 128 ? (char)ch : '?'));
    }
    while(!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static bool ScreenHas(const char* needle){
    for(int y = 0; y < g_term.rows(); y++){
        if(TermRowText(y).find(needle) != std::string::npos) return true;
    }
    for(int i = 0; i < g_term.scrollbackCount(); i++){
        std::string s;
        const VtTerminal::Cell* row = g_term.scrollbackRow(i);
        for(int x = 0; x < g_term.cols(); x++) s.push_back(row[x].ch && row[x].ch < 128 ? (char)row[x].ch : ' ');
        if(s.find(needle) != std::string::npos) return true;
    }
    return false;
}

int main(int argc, char** argv){
    if(argc < 5){
        fprintf(stderr, "usage: %s <port> <key> <user> <hostkey.pub>\n", argv[0]);
        return 2;
    }
    const uint16_t port = (uint16_t)atoi(argv[1]);
    const char* key_path = argv[2];
    const char* user = argv[3];
    const std::string host_pub_line = ReadFile(argv[4]);

    // SDはstubsのメモリ上のもの。鍵ファイルとknown_hostsをそこへ置く
    OSData::SD_usable = true;
    HostSd::files["/sys/ssh/id_ed25519"] = ReadFile(key_path);

    printf("===== 秘密鍵の読み込み =====\n");
    uint8_t secret[64], pub[32];
    CHECK(SshUtil::LoadPrivateKey("/sys/ssh/id_ed25519", secret, pub) == SshUtil::KeyResult::Ok);
    CHECK(SshUtil::LoadPrivateKey("/sys/ssh/none", secret, pub) == SshUtil::KeyResult::NotFound);
    {
        const char* rsa = "-----BEGIN RSA PRIVATE KEY-----\nAAAA\n-----END RSA PRIVATE KEY-----\n";
        uint8_t s2[64], p2[32];
        CHECK(SshUtil::ParsePrivateKey(rsa, strlen(rsa), s2, p2) == SshUtil::KeyResult::Unsupported);
    }

    printf("===== 鍵交換とホスト鍵 =====\n");
    g_term.resize(40, 12);
    SshClient* c = new SshClient();
    c->setOutput(OnOutput, nullptr);
    c->setIdentity(secret, pub);
    c->setTerminalSize(40, 12, 240, 192);
    CHECK(c->connect("127.0.0.1", port, user));
    Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::HostKeyCheck || s.state() == SshClient::State::Closed; });
    CHECK(c->state() == SshClient::State::HostKeyCheck);
    printf("  (server: %s / err: %s)\n", c->serverVersion(), c->errorText());
    CHECK(strncmp(c->serverVersion(), "SSH-2.0-OpenSSH", 15) == 0);

    // ホスト鍵がsshdの.pubと一致すること
    {
        uint8_t blob[64];
        const size_t sp1 = host_pub_line.find(' ');
        const size_t sp2 = host_pub_line.find(' ', sp1 + 1);
        const std::string b64 = host_pub_line.substr(sp1 + 1, sp2 - sp1 - 1);
        const int bn = SshUtil::Base64Decode(b64.c_str(), b64.size(), blob, sizeof(blob));
        CHECK(bn == (int)SshUtil::kEd25519BlobBytes);
        CHECK(memcmp(blob + 19, c->hostKey(), 32) == 0);
    }
    char fp[80];
    SshUtil::Fingerprint(c->hostKey(), fp, sizeof(fp));
    printf("  fingerprint: %s\n", fp);
    const char* expected_fp = getenv("SSH_TEST_FINGERPRINT");
    if(expected_fp) CHECK(strcmp(fp, expected_fp) == 0);

    // known_hosts: 初めては Unknown、追記すると Match、別の鍵なら Mismatch
    const char* kh = "/sys/ssh/known_hosts";
    CHECK(SshUtil::CheckKnownHost(kh, "127.0.0.1", port, c->hostKey()) == SshUtil::HostStatus::Unknown);
    CHECK(SshUtil::AddKnownHost(kh, "127.0.0.1", port, c->hostKey()));
    CHECK(SshUtil::CheckKnownHost(kh, "127.0.0.1", port, c->hostKey()) == SshUtil::HostStatus::Match);
    {
        uint8_t other[32];
        memcpy(other, c->hostKey(), 32);
        other[0] ^= 1;
        CHECK(SshUtil::CheckKnownHost(kh, "127.0.0.1", port, other) == SshUtil::HostStatus::Mismatch);
        CHECK(SshUtil::CheckKnownHost(kh, "localhost", port, other) == SshUtil::HostStatus::Unknown);
    }
    printf("  known_hosts: %s", HostSd::files[kh].c_str());
    CHECK(HostSd::files[kh].find("[127.0.0.1]:") == 0);

    c->acceptHostKey(true);

    printf("===== 公開鍵認証とシェル =====\n");
    Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::Open || s.state() == SshClient::State::Closed; });
    CHECK(c->state() == SshClient::State::Open);
    if(c->state() != SshClient::State::Open) printf("  err: %s\n", c->errorText());

    c->send("echo pico$((40+2))os\r");
    CHECK(WaitFor(*c, "pico42os"));
    CHECK(ScreenHas("pico42os"));

    // 端末の大きさ
    c->send("stty size\r");
    CHECK(WaitFor(*c, "12 40"));
    c->setTerminalSize(30, 7, 240, 112);
    g_term.resize(30, 7);
    c->send("stty size\r");
    CHECK(WaitFor(*c, "7 30"));

    // 大きな出力(受信窓 16KB を何度も跨ぐ)
    c->send("seq 1 20000 | tail -c 30000 | wc -c; seq 1 6000 | md5sum\r");
    CHECK(WaitFor(*c, "30000"));
    {
        // 期待値は母艦で計算する
        FILE* p = popen("seq 1 6000 | md5sum | cut -c1-32", "r");
        char md5[40] = {0};
        if(p){ fgets(md5, sizeof(md5), p); pclose(p); }
        md5[32] = '\0';
        CHECK(WaitFor(*c, md5));
    }
    // 大量の出力そのもの
    g_out.clear();
    c->send("seq 1 5000; echo DONE-$((1+1))-MARK\r"); // 打った行のエコーと区別するため計算させる
    CHECK(WaitFor(*c, "DONE-2-MARK\r\n"));
    CHECK(g_out.find("\r\n4999\r\n5000\r\n") != std::string::npos);

    // UTF-8(全角)が端末に2セルで入る
    c->send("printf '\\343\\201\\202A\\n'\r");
    CHECK(WaitFor(*c, "\xe3\x81\x82" "A"));

    printf("===== 終了 =====\n");
    c->send("exit 3\r");
    Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::Closed; });
    CHECK(c->state() == SshClient::State::Closed);
    CHECK(c->exitStatus() == 3);
    printf("  %s\n", c->errorText());
    delete c;

    printf("===== ホスト鍵を信頼しない =====\n");
    c = new SshClient();
    c->setOutput(OnOutput, nullptr);
    c->setIdentity(secret, pub);
    CHECK(c->connect("127.0.0.1", port, user));
    Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::HostKeyCheck || s.state() == SshClient::State::Closed; });
    c->acceptHostKey(false);
    CHECK(c->state() == SshClient::State::Closed);
    delete c;

    printf("===== 鍵なし(パスワード認証も無いサーバ) =====\n");
    c = new SshClient();
    c->setOutput(OnOutput, nullptr);
    CHECK(c->connect("127.0.0.1", port, user));
    Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::HostKeyCheck || s.state() == SshClient::State::Closed; });
    c->acceptHostKey(true);
    Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::Closed || s.state() == SshClient::State::NeedPassword; });
    CHECK(c->state() == SshClient::State::Closed);
    printf("  %s\n", c->errorText());
    CHECK(strstr(c->errorText(), "publickey") != nullptr);
    delete c;

    // パスワード認証(任意)。母艦に利用者を作る必要があるので、環境変数で指定されたときだけ
    //   SSH_TEST_PW_PORT … パスワード/keyboard-interactiveを受け付けるsshdのポート
    //   SSH_TEST_PW_USER / SSH_TEST_PW_PASS
    if(getenv("SSH_TEST_PW_PORT")){
        printf("===== パスワード認証 =====\n");
        const uint16_t pw_port = (uint16_t)atoi(getenv("SSH_TEST_PW_PORT"));
        const char* pw_user = getenv("SSH_TEST_PW_USER");
        const char* pw_pass = getenv("SSH_TEST_PW_PASS");
        c = new SshClient();
        c->setOutput(OnOutput, nullptr);
        CHECK(c->connect("127.0.0.1", pw_port, pw_user));
        Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::HostKeyCheck || s.state() == SshClient::State::Closed; });
        c->acceptHostKey(true);
        Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::NeedPassword || s.state() == SshClient::State::Closed; });
        CHECK(c->state() == SshClient::State::NeedPassword);
        printf("  prompt: %s\n", c->promptText());
        c->providePassword("wrong-password");
        Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::NeedPassword || s.state() == SshClient::State::Closed; }, 15000);
        CHECK(c->state() == SshClient::State::NeedPassword); // 違えばもう一度聞かれる
        c->providePassword(pw_pass);
        Pump(*c, [](SshClient& s){ return s.state() == SshClient::State::Open || s.state() == SshClient::State::Closed || s.state() == SshClient::State::NeedPassword; });
        CHECK(c->state() == SshClient::State::Open);
        if(c->state() != SshClient::State::Open) printf("  err: %s / prompt: %s\n", c->errorText(), c->promptText());
        c->send("echo pw-$((1+1))-ok; exit\r");
        CHECK(WaitFor(*c, "pw-2-ok"));
        delete c;
    }

    printf("===== 踏み台(ProxyJump) =====\n");
    {
        SshClient* jump = new SshClient();
        SshTunnel* tunnel = new SshTunnel();
        std::string jump_out;
        jump->setOutput([](void* ctx, const uint8_t* d, size_t n){ ((std::string*)ctx)->append((const char*)d, n); }, &jump_out);
        jump->setIdentity(secret, pub);
        tunnel->attach(jump);
        jump->setForward("localhost", port); // 名前は踏み台の側で引かれる(Tailscaleなら MagicDNS の名前)
        CHECK(jump->connect("127.0.0.1", port, user));
        Pump(*jump, [](SshClient& s){ return s.state() == SshClient::State::HostKeyCheck || s.state() == SshClient::State::Closed; });
        CHECK(jump->state() == SshClient::State::HostKeyCheck);
        jump->acceptHostKey(true);
        Pump(*jump, [](SshClient& s){ return s.state() == SshClient::State::Open || s.state() == SshClient::State::Closed; });
        CHECK(jump->state() == SshClient::State::Open);
        if(jump->state() != SshClient::State::Open) printf("  err: %s\n", jump->errorText());

        g_out.clear();
        c = new SshClient();
        c->setOutput(OnOutput, nullptr);
        c->setIdentity(secret, pub);
        c->setTerminalSize(40, 12, 240, 192);
        CHECK(c->connectVia(tunnel, user));
        // 2本を交互に回す(実際のSshSceneと同じ順: 踏み台 → 中)
        for(int i = 0; i < 2000 && c->state() != SshClient::State::HostKeyCheck && c->state() != SshClient::State::Closed; i++){
            jump->update(); c->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(c->state() == SshClient::State::HostKeyCheck);
        CHECK(memcmp(c->hostKey(), jump->hostKey(), 32) == 0); // 同じsshdなので同じ鍵
        c->acceptHostKey(true);
        for(int i = 0; i < 2000 && c->state() != SshClient::State::Open && c->state() != SshClient::State::Closed; i++){
            jump->update(); c->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(c->state() == SshClient::State::Open);
        if(c->state() != SshClient::State::Open) printf("  err: %s / jump: %s\n", c->errorText(), jump->errorText());

        auto wait2 = [&](const char* needle){
            for(int i = 0; i < 3000 && g_out.find(needle) == std::string::npos && c->state() != SshClient::State::Closed; i++){
                jump->update(); c->update();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            return g_out.find(needle) != std::string::npos;
        };
        c->send("echo via-$((6*7))-jump\r");
        CHECK(wait2("via-42-jump"));
        c->send("stty size\r");
        CHECK(wait2("12 40"));
        // 通り道の受信窓(8KB)を何度も跨ぐ大きな出力
        g_out.clear();
        c->send("seq 1 20000; echo JUMP-$((1+1))-END\r"); // 打った行のエコーと区別するため計算させる
        CHECK(wait2("JUMP-2-END\r\n"));
        CHECK(g_out.find("\r\n19999\r\n20000\r\n") != std::string::npos);
        CHECK(jump->isOpen());

        c->send("exit 5\r");
        for(int i = 0; i < 2000 && c->state() != SshClient::State::Closed; i++){
            jump->update(); c->update();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(c->state() == SshClient::State::Closed);
        CHECK(c->exitStatus() == 5);
        delete c;  // 中→通り道→踏み台の順に片付ける(中の片付けで踏み台も切れる)
        CHECK(jump->state() == SshClient::State::Closed);
        delete tunnel;
        delete jump;
    }
    {
        // 踏み台から繋げない相手
        SshClient* jump = new SshClient();
        SshTunnel* tunnel = new SshTunnel();
        jump->setOutput(OnOutput, nullptr);
        jump->setIdentity(secret, pub);
        tunnel->attach(jump);
        jump->setForward("127.0.0.1", 1);
        CHECK(jump->connect("127.0.0.1", port, user));
        Pump(*jump, [](SshClient& s){ return s.state() == SshClient::State::HostKeyCheck || s.state() == SshClient::State::Closed; });
        jump->acceptHostKey(true);
        Pump(*jump, [](SshClient& s){ return s.state() == SshClient::State::Open || s.state() == SshClient::State::Closed; });
        CHECK(jump->state() == SshClient::State::Closed);
        printf("  %s\n", jump->errorText());
        CHECK(strstr(jump->errorText(), "踏み台から 127.0.0.1:1") != nullptr);
        delete tunnel;
        delete jump;
    }

    printf("===== 繋がらない相手 =====\n");
    c = new SshClient();
    CHECK(!c->connect("127.0.0.1", 1, user, 1000));
    CHECK(c->state() == SshClient::State::Closed);
    delete c;

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
