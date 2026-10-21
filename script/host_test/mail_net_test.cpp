// メールアプリの受信係(ImapClient)を本物のソケットで確かめる結合テスト。run_net.sh から呼ぶ。
//
// **本物のソケットとTLS(OpenSSL、pc/compat/)を使う。** 相手は script/host_test/imap_fake_server.py
// (IMAPの偽物。run_net.sh が平文とTLSで立てる。TLSは使い捨てのCAで作った証明書)。本物のメールサーバへは行かない。
//
// 見ること:
//   - パスワードが違えば AuthError になり、勝手に繋ぎ直さない(サーバに締め出されないように)
//   - 一覧は新しい順に kMaxMessages 通。全体の通数・既読/未読・差出人の名前・件名(ISO-2022-JP / UTF-8)
//   - 本文: multipart から text/plain と添付の名前、HTMLだけのメール、ISO-2022-JP、無いUIDは理由つきで失敗
//   - mark-read = true なら開いたメールが既読になり、false(BODY.PEEK)なら未読のまま
//   - **接続を使い回す**(本文を何通開いても LOGIN し直さない)。TLSでも同じ
//   - 新着: サーバにメールが増えたら、更新(NOOP → EXISTS)で一覧を取り直す
//   - /sys/mail.cfg を読み、平文のパスワードは暗号化して書き直す。8bitの文字のパスワードは断る
//
// 使い方: mail_net_test <平文のport> <TLSのport> <ca.pem> <平文のサーバのspool>
#include "apps/mail/Imap_Client.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstdlib>
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
static void contains(const char* a, const char* part, const char* label){
    const bool ok = strstr(a, part) != nullptr;
    printf("%s %-46s 実測=\"%.80s\"\n", ok ? "[ OK ]" : "[FAIL]", label, a);
    if(!ok) failures++;
}

using S = ImapClient::State;

// millis() はスタブで進まないので、回数で打ち切る(1回1msで最大10秒)
static bool runUntil(ImapClient& c, const std::function<bool()>& done){
    for(int i = 0; i < 10000; i++){
        c.update(true);
        if(done()) return true;
        usleep(1000);
    }
    return false;
}

static ImapClient::Config account(const char* host, uint16_t port, bool tls, const char* password, bool mark_read){
    ImapClient::Config cfg;
    cfg.host.assign(host);
    cfg.port = port;
    cfg.tls = tls;
    cfg.user.assign("me@example.jp");
    cfg.password.assign(password);
    cfg.mark_read = mark_read;
    return cfg;
}

// 本文を開いて、取れるまで回す
static bool openBody(ImapClient& c, uint32_t uid){
    const uint32_t rev = c.bodyRevision();
    c.openMessage(uid);
    return runUntil(c, [&]{ return c.bodyRevision() != rev && !c.bodyLoading(); }) && c.bodyUid() == uid;
}

static void scenario(const char* name, const char* host, uint16_t port, bool tls, bool mark_read, const char* spool){
    printf("\n---- %s ----\n", name);

    //パスワード違い
    {
        static ImapClient bad;
        bad.configure(account(host, port, tls, "wrong", mark_read));
        check(runUntil(bad, [&]{ return bad.state() == S::AuthError; }), "パスワードが違えば AuthError");
        contains(bad.statusText(), "AUTHENTICATIONFAILED", "サーバの理由が出る");
        for(int i = 0; i < 300; i++){ bad.update(true); usleep(1000); }
        check(bad.state() == S::AuthError && !bad.connected(), "勝手に繋ぎ直さない");
        bad.openMessage(123);
        runUntil(bad, [&]{ return !bad.bodyLoading(); });
        check(!bad.bodyOk() && !bad.connected(), "ログインできない間は本文も取りに行かない");
        bad.stop();
    }

    static ImapClient c;
    check(c.configure(account(host, port, tls, "secret", mark_read)), "設定を受け付ける");
    check(runUntil(c, [&]{ return c.loaded() || c.state() == S::Error; }) && c.state() == S::Ok, "一覧を取れる");
    eq_int(c.messageCount(), ImapClient::kMaxMessages, "新しい方から kMaxMessages 通");
    eq_int(c.totalCount(), 25, "全体の通数");
    bool sorted = true;
    for(int i = 1; i < c.messageCount(); i++) if(c.messageAt(i - 1).seq <= c.messageAt(i).seq) sorted = false;
    check(sorted, "新しい順に並ぶ");
    eq_int(c.messageAt(0).seq, 25, "先頭は一番新しいメール");
    eq_int(c.messageAt(0).uid, 101 + 24 * 3, "UIDは番号と別に持つ");
    eq_str(c.messageAt(0).subject.c_str(), "シフトJISのメール", "件名(ISO-2022-JPのB)");
    eq_str(c.messageAt(0).from.c_str(), "sjis@example.jp", "差出人(名前なし)");
    eq_str(c.messageAt(2).from.c_str(), "ショップ", "差出人の名前(UTF-8のB)");
    eq_str(c.messageAt(1).subject.c_str(), "週刊ニュース #42", "件名(Q)");
    eq_str(c.messageAt(3).from.c_str(), "山田 太郎", "差出人の名前(ISO-2022-JP)");
    eq_int(c.unseenCount(), 14, "未読の数(古い6通は既読)");
    check(c.messageAt(0).date > 1577836800, "届いた日時");

    const uint32_t shop = c.messageAt(2).uid;
    check(openBody(c, shop), "本文(multipart)を取れる");
    check(c.bodyOk(), "本文: 成功");
    contains(c.bodyText(), "注文番号: 123-4567", "本文: text/plain を使う");
    check(!c.bodyFromHtml() && !c.bodyTruncated(), "本文: HTMLではない・切れていない");
    eq_int(c.bodyInfo().attachmentCount(), 1, "本文: 添付は1つ");
    eq_str(c.bodyInfo().attachmentNames(), "領収書.pdf", "本文: 添付の名前(RFC2231)");
    eq_str(c.bodyInfo().to(), "me@example.jp", "本文: 宛先");
    const Imap::Summary* s = c.findMessage(shop);
    check(s && s->seen == mark_read, mark_read ? "mark-read: 開いたら既読" : "mark-read=false: 開いても未読");
    eq_int(c.unseenCount(), mark_read ? 13 : 14, "未読の数が追従する");

    check(openBody(c, c.messageAt(1).uid) && c.bodyOk() && c.bodyFromHtml(), "HTMLだけのメール");
    contains(c.bodyText(), "・ラズパイの新製品", "HTML: 箇条書き");
    check(strstr(c.bodyText(), "color") == nullptr, "HTML: style は出さない");
    check(openBody(c, c.messageAt(3).uid) && c.bodyOk(), "ISO-2022-JP のメール");
    check(strncmp(c.bodyText(), "山田です。", strlen("山田です。")) == 0, "ISO-2022-JP: 本文");
    check(openBody(c, 99999) && !c.bodyOk(), "無いUIDは失敗");
    contains(c.bodyError(), "見つかりません", "無いUID: 理由");
    check(c.connected(), "本文を何通開いても接続を使い回す");

    //閉じた後に結果が届いても、次に開いたメールを取り違えない
    c.openMessage(c.messageAt(0).uid);
    c.closeMessage();
    check(openBody(c, c.messageAt(3).uid) && strncmp(c.bodyText(), "山田です。", strlen("山田です。")) == 0,
          "取り消した後に開いたメールが出る");

    if(spool){
        //新着: spool にメールを足して、更新で取り直す
        const std::string path = std::string(spool) + "/026.eml";
        FILE* f = fopen(path.c_str(), "wb");
        fputs("From: \"New Sender\" <new@example.jp>\r\nSubject: =?UTF-8?B?5paw552A?=\r\n"
              "Content-Type: text/plain; charset=UTF-8\r\n\r\nnew mail\r\n", f);
        fclose(f);
        const uint32_t rev = c.listRevision();
        c.refreshNow();
        check(runUntil(c, [&]{ return c.listRevision() != rev && c.totalCount() == 26 && !c.busy(); }), "新着で一覧を取り直す");
        eq_str(c.messageAt(0).subject.c_str(), "新着", "新着が先頭");
        eq_str(c.messageAt(0).from.c_str(), "New Sender", "新着の差出人");
        check(!c.messageAt(0).seen, "新着は未読");
        eq_int(c.messageCount(), ImapClient::kMaxMessages, "一覧は kMaxMessages 通のまま");
        //変わっていなければ取り直さない(NOOPだけ)
        const uint32_t rev2 = c.listRevision();
        c.refreshNow();
        runUntil(c, [&]{ return !c.busy(); });
        for(int i = 0; i < 100; i++){ c.update(true); usleep(1000); }
        check(c.listRevision() != rev2, "[更新] は必ず取り直す");
    }

    c.stop();
    check(!c.connected(), "stop() で切る");
}

static void config(uint16_t port){
    printf("\n---- /sys/mail.cfg ----\n");
    const char* path = PICO_Path::FILE::CFG::SYS_MAIL_CFG;

    static ImapClient none;
    HostSd::files.erase(path);
    check(!none.loadConfig() && none.state() == S::NotConfigured, "mail.cfg が無ければ NotConfigured");

    HostSd::files[path] = "# テスト\nhost = 127.0.0.1\nport = " + std::to_string(port)
                        + "\ntls = false\nuser = me@example.jp\npassword = secret\nmark-read = false\n";
    static ImapClient c;
    check(c.loadConfig(), "平文のパスワードを読める");
    check(!c.config().mark_read && !c.config().tls && c.config().port == port, "ほかの設定も読める");
    const std::string saved = HostSd::files[path];
    check(saved.find("password=enc") != std::string::npos && saved.find("secret") == std::string::npos,
          "平文のパスワードは暗号化して書き直す");
    check(saved.find("mark-read = false") != std::string::npos, "ほかの行は残す");

    static ImapClient c2;
    check(c2.loadConfig(), "暗号化したパスワードを読める");
    eq_str(c2.config().password.c_str(), "secret", "復号したパスワード");
    check(runUntil(c2, [&]{ return c2.loaded() || c2.state() == S::Error; }) && c2.state() == S::Ok, "暗号化したパスワードで繋がる");
    c2.stop();

    //設定画面からの保存(サーバとユーザー名だけ変えるとパスワードは残る)
    check(ImapClient::SaveAccount("imap.example.jp", 993, "other@example.jp", ""), "SaveAccount(パスワードは空)");
    static ImapClient c3;
    c3.loadConfig();
    check(c3.config().host == "imap.example.jp" && c3.config().port == 993 && c3.config().user == "other@example.jp"
          && c3.config().password == "secret", "パスワードは今のまま");

    ImapClient::Config jp = account("127.0.0.1", port, false, "パスワード", true);
    static ImapClient c4;
    check(!c4.configure(jp) && c4.state() == S::NotConfigured, "8bitの文字のパスワードは断る");
    contains(c4.statusText(), "パスワード", "断った理由");
}

int main(int argc, char** argv){
    if(argc < 5){
        fprintf(stderr, "使い方: %s <平文のport> <TLSのport> <ca.pem> <平文のspool>\n", argv[0]);
        return 2;
    }
    std::ifstream ca_in(argv[3]);
    std::stringstream ca;
    ca << ca_in.rdbuf();
    OSData::SD_usable = true;
    HostSd::files[PICO_Path::FILE::TLS_EXTRA_CA_PEM] = ca.str();

    const uint16_t plain = (uint16_t)atoi(argv[1]);
    const uint16_t tls = (uint16_t)atoi(argv[2]);
    scenario("IMAP(平文、mark-read)", "127.0.0.1", plain, false, true, argv[4]);
    scenario("IMAPS(TLS、mark-read=false)", "localhost", tls, true, false, nullptr);
    config(plain);

    printf("\n%s (失敗 %d件)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
