// チャットの通信係(ChatClient)を本物のサーバ相手に確かめる結合テスト。run_net.sh から呼ぶ。
//
// **本物のソケットとTLS(OpenSSL、pc/compat/)を使う。** 相手は server/chat/chat_server.py
// (run_net.sh が使い捨てのDBで立てる。HTTPSは使い捨てのCAで作った証明書)。
//
// 見ること:
//   - 部屋の一覧(未読の数)と発言を取れる。発言の本文の改行・\ が往復で崩れない
//   - 送った発言が、別の人の端末から見える
//   - **接続を使い回す**(keep-alive)。サーバが無通信で閉じた後も、繋ぎ直して続けられる
//   - トークンが違えば AuthError になり、勝手に取り直さない
//   - HTTPS でも同じように動き、接続を使い回す
//
// 使い方: chat_net_test <httpのport> <httpsのport> <ca.pem> <aliceのトークン> <bobのトークン>
#include "chat/Chat_Client.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <unistd.h>

// ---- モック ----
void LogFunctions::Log(LogType, const char* fmt, ...){
    va_list args;
    va_start(args, fmt);
    printf("    [log] ");
    vprintf(fmt, args);
    printf("\n");
    va_end(args);
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-46s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}
static void eq_str(const char* a, const char* e, const char* label){
    const bool ok = strcmp(a, e) == 0;
    printf("%s %-46s 実測=\"%s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a);
    if(!ok) failures++;
}

// millis() はスタブで進まないので、回数で打ち切る(1回1msで最大10秒)
static bool runUntil(ChatClient& c, const std::function<bool()>& done){
    for(int i = 0; i < 10000; i++){
        c.update(true);
        if(done()) return true;
        usleep(1000);
    }
    return false;
}

static bool fetchRooms(ChatClient& c){
    const uint32_t rev = c.roomsRevision();
    c.refreshNow();
    return runUntil(c, [&]{ return c.roomsRevision() != rev || c.state() == ChatClient::State::Error
                                   || c.state() == ChatClient::State::AuthError; })
        && c.state() == ChatClient::State::Ok;
}

static bool fetchMessages(ChatClient& c){
    const uint32_t rev = c.messagesRevision();
    const bool first = !c.messagesLoaded();
    c.refreshNow();
    return runUntil(c, [&]{
        if(c.state() != ChatClient::State::Ok) return true;
        return first ? c.messagesLoaded() : c.messagesRevision() != rev;
    }) && c.state() == ChatClient::State::Ok;
}

static const ChatProto::Message* last(ChatClient& c){
    return c.messageCount() ? &c.messageAt(c.messageCount() - 1) : nullptr;
}

static void scenario(const char* label, const std::string& server, const char* alice_token, const char* bob_token,
                     bool test_idle_reconnect){
    printf("\n---- %s (%s) ----\n", label, server.c_str());

    ChatClient alice;
    check(alice.configure(server.c_str(), alice_token), "設定を受け付ける");

    check(fetchRooms(alice), "部屋の一覧を取れる");
    eq_int(alice.roomCount(), 2, "部屋は2つ");
    if(alice.roomCount() == 2){
        eq_str(alice.roomAt(0).name.c_str(), "雑談", "1つ目の部屋の名前");
    }
    check(alice.connectionKept(), "応答の後も接続を持っている(keep-alive)");

    const uint32_t room = alice.roomCount() ? alice.roomAt(0).id : 1;
    alice.setRoom(room);
    check(fetchMessages(alice), "部屋を開くと発言を取れる");
    const int before = alice.messageCount();

    //本文の改行と \ が往復で崩れないこと(サーバはエスケープして返す)
    const char* text = "こんにちは\n2行目 \\ おわり";
    check(alice.send(text), "発言を送れる");
    check(alice.sending(), "送信中になる");
    check(!alice.send("二重"), "送信中は次の発言を受け付けない");
    check(runUntil(alice, [&]{ return !alice.sending() && alice.messageCount() > before; }), "送った発言が一覧に出る");
    if(const ChatProto::Message* m = last(alice)){
        eq_str(m->text.c_str(), text, "本文が崩れない");
        eq_str(m->name.c_str(), "ありす", "発言者の表示名");
    }
    check(alice.connectionKept(), "送信の後も接続を持っている");

    std::string too_long(ChatProto::kMaxTextBytes + 1, 'x');
    check(!alice.send(too_long.c_str()), "長すぎる発言は送らない");

    // ---- 別の人から見える ----
    ChatClient bob;
    check(bob.configure(server.c_str(), bob_token), "bob: 設定を受け付ける");
    bob.setRoom(room);
    check(fetchMessages(bob), "bob: 部屋を開ける");
    if(const ChatProto::Message* m = last(bob)){
        eq_str(m->text.c_str(), text, "bob: aliceの発言が見える");
    }
    check(bob.send("返事です"), "bob: 返事を送れる");
    check(runUntil(bob, [&]{ return !bob.sending(); }), "bob: 送り終わる");

    // ---- サーバが無通信の接続を閉じた後(idle-timeout=1秒) ----
    if(test_idle_reconnect){
        sleep(2);
        check(fetchMessages(alice), "サーバが閉じた接続でも、繋ぎ直して取れる");
        if(const ChatProto::Message* m = last(alice)){
            eq_str(m->text.c_str(), "返事です", "bobの返事が届く");
        }
    }else{
        check(fetchMessages(alice), "bobの返事を取れる");
    }

    // ---- 未読の数 ----
    alice.setRoom(0);
    const uint32_t other = alice.roomCount() >= 2 ? alice.roomAt(1).id : 2;
    bob.setRoom(other);
    check(fetchMessages(bob), "bob: 別の部屋を開ける");
    check(bob.send("別の部屋への発言"), "bob: 別の部屋へ送る");
    check(runUntil(bob, [&]{ return !bob.sending(); }), "bob: 送り終わる");
    check(fetchRooms(alice), "一覧を取り直せる");
    const ChatProto::Room* r1 = alice.findRoom(room);
    const ChatProto::Room* r2 = alice.findRoom(other);
    check(r1 && r1->unread == 0, "読んだ部屋の未読は0");
    check(r2 && r2->unread >= 1, "bobが書いた別の部屋には未読がある");

    // ---- トークン違い ----
    ChatClient bad;
    bad.configure(server.c_str(), "wrong-token");
    bad.refreshNow();
    check(runUntil(bad, [&]{ return bad.state() == ChatClient::State::AuthError; }), "トークンが違えば AuthError");
    check(strstr(bad.statusText(), "トークン") != nullptr, "理由(サーバの1行目)を見せる");
    const uint32_t rev = bad.roomsRevision();
    for(int i = 0; i < 50; i++){ bad.update(true); usleep(1000); }
    eq_int(bad.roomsRevision(), rev, "AuthError の後は勝手に取り直さない");

    alice.stop();
    check(!alice.connectionKept(), "stop() で接続を閉じる");
    bob.stop();
}

int main(int argc, char** argv){
    if(argc < 6){
        fprintf(stderr, "使い方: %s <http port> <https port> <ca.pem> <alice token> <bob token>\n", argv[0]);
        return 2;
    }
    std::ifstream ca_in(argv[3]);
    std::stringstream ca;
    ca << ca_in.rdbuf();
    OSData::SD_usable = true;
    HostSd::files[PICO_Path::FILE::TLS_EXTRA_CA_PEM] = ca.str();

    // ---- 設定の読み込み(/sys/chat.cfg) ----
    {
        ChatClient c;
        HostSd::files[PICO_Path::FILE::CFG::SYS_CHAT_CFG] =
            std::string("# テスト\nserver = http://127.0.0.1:") + argv[1] + "/\ntoken = " + argv[4] + "\npoll-ms = 5000\n";
        check(c.loadConfig(), "chat.cfg を読める(末尾の / は落とす)");
        check(fetchRooms(c), "chat.cfg の設定で繋がる");
        c.stop();
    }

    scenario("HTTP", std::string("http://127.0.0.1:") + argv[1], argv[4], argv[5], true);
    scenario("HTTPS", std::string("https://localhost:") + argv[2], argv[4], argv[5], false);

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
