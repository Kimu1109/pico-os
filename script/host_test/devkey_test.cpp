// 端末ごとの鍵(DeviceKeyFunctions / util/Device_Key.hpp)。OTPはメモリの中で真似る。
//   - 鍵を作る前は固定鍵(enc1: / Luaの暗号 version 1 / PINの v1)のまま
//   - 作るとOTPの枠に書き、SDの暗号化された設定を enc3: へ書き直す(コメント・他のキーは残す)
//   - 起動し直しても同じ鍵が読める・もう一度は作れない
//   - 端末の鍵が無いと enc3: / version 3 / v2 は読めない(PINはロックしない)
//   - PINの v1 は正しく打ったときに v2 へ書き直す
//   - 書きかけの枠は次の枠へ作れる・両方壊れたら Broken
#include "functions/DeviceKey_Functions.hpp"
#include "functions/Lock_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "ssh/Ssh_Util.hpp"
#include "storage/SD_Path.hpp"
#include "util/Device_Key.hpp"
#include "util/Secret_Aead.hpp"
#include "util/Secret_Cipher.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ---- 偽物 ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
bool PowerFunctions::IsSleeping(){ return false; }
void PadFunctions::SuppressThisFrame(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static std::string Enc(const char* purpose, const char* plain){
    char buf[600];
    if(!PICO_Secret::Encrypt(purpose, plain, buf, sizeof(buf))) return "";
    return buf;
}

static std::string Dec(const char* purpose, const std::string& v){
    char buf[300];
    if(!PICO_Secret::Decrypt(purpose, v.c_str(), buf, sizeof(buf))) return "<fail>";
    return buf;
}

// key=value の値を読む(無ければ空)
static std::string ValueOf(const char* path, const char* key){
    std::string out;
    const std::string& body = HostSd::files[path];
    size_t pos = 0;
    while(pos < body.size()){
        size_t end = body.find('\n', pos);
        if(end == std::string::npos) end = body.size();
        const std::string line = body.substr(pos, end - pos);
        char k[64], v[600];
        if(PICO_Config::ParseLine(line.c_str(), k, sizeof(k), v, sizeof(v)) == PICO_Config::ConfigLineResult::Entry &&
           strcmp(k, key) == 0) out = v;
        pos = end + 1;
    }
    return out;
}

static bool Starts(const std::string& s, const char* p){ return s.compare(0, strlen(p), p) == 0; }

int main(){
    using namespace DeviceKeyFunctions;
    using PICO_Path::FILE::CFG::SYS_WIFI_CFG;
    HostSd::files.clear();
    OSData::SD_usable = true;
    Test::ResetOtp();

    // ---- 作る前 ----
    Setup();
    check(GetState() == State::None && !DeviceKey::available, "鍵が無ければ None");
    check(Starts(Enc("p", "abc"), "enc1:"), "鍵が無ければ enc1:(固定鍵)");
    check(PICO_Secret::NeedsUpgrade("plain") && !PICO_Secret::NeedsUpgrade(Enc("p", "x").c_str()) && !PICO_Secret::NeedsUpgrade(""),
          "鍵が無ければ平文だけが書き直しの対象");

    // SDへ固定鍵/平文の設定を置く
    const std::string url = "https://calendar.google.com/calendar/ical/abc%40group/private-0123456789abcdef/basic.ics";
    HostSd::files[SYS_WIFI_CFG] = "# コメント\nenabled=true\nssid1=" + Enc("wifi-ssid:1", "HomeNet") +
                                  "\npass1=" + Enc("wifi-password:1", "secret-pass") + "\nssid2=Office\npass2=\n";
    HostSd::files[PICO_Path::FILE::CFG::SYS_NETWORK_CFG] = "timezone=JST-9\nwifi-password=" + Enc("wifi-password", "old") + "\n";
    HostSd::files[PICO_Path::FILE::CFG::SYS_CHAT_CFG] = "server=https://chat.example\ntoken=" + Enc("chat-token", "tok123") + "\n";
    HostSd::files[PICO_Path::FILE::CFG::SYS_TODOIST_CFG] = "token=0123456789abcdef0123456789abcdef01234567\nreminders=true\n";
    HostSd::files[PICO_Path::FILE::CALENDAR_SOURCES] = "family = " + Enc("calendar-url:family", url.c_str()) + "\n";
    uint8_t secret[64];
    for(int i = 0; i < 64; i++) secret[i] = (uint8_t)(i * 7 + 1);
    char ssh_enc[SshUtil::kEncryptedKeyCap];
    SshUtil::EncryptKey(secret, ssh_enc, sizeof(ssh_enc));
    HostSd::files[PICO_Path::FILE::SSH_ID_ED25519] = std::string(ssh_enc) + "\n";
    const uint8_t salt[16] = {3};
    char pin_rec[128];
    LockFunctions::FormatRecord("2580", salt, pin_rec, sizeof(pin_rec));
    LockFunctions::SetRecordForTest(pin_rec);
    // Luaの暗号(パスワード無し)
    std::string lua_v1;
    SecretAead::Encrypt((const uint8_t*)"save", 4, "/lua/apps/x", nullptr, lua_v1);

    // ---- 作る ----
    char msg[256];
    check(Provision(msg, sizeof(msg)), "鍵を作れる");
    printf("       (%s)\n", msg);
    check(GetState() == State::Ready && DeviceKey::available, "作ると Ready");
    check(strstr(msg, "PIN") != nullptr, "PINは次に解除したときと伝える");

    const std::string s1 = ValueOf(SYS_WIFI_CFG, "ssid1"), p1 = ValueOf(SYS_WIFI_CFG, "pass1"), s2 = ValueOf(SYS_WIFI_CFG, "ssid2");
    check(Starts(s1, "enc3:") && Starts(p1, "enc3:") && Starts(s2, "enc3:"), "wifi.cfg が enc3: に(平文の項目も)");
    check(Dec("wifi-ssid:1", s1) == "HomeNet" && Dec("wifi-password:1", p1) == "secret-pass" && Dec("wifi-ssid:2", s2) == "Office",
          "wifi.cfg の中身は同じ");
    check(ValueOf(SYS_WIFI_CFG, "pass2").empty(), "空の値は空のまま");
    check(HostSd::files[SYS_WIFI_CFG].find("# コメント") == 0 && ValueOf(SYS_WIFI_CFG, "enabled") == "true", "コメントと他のキーは残る");
    check(Dec("wifi-password", ValueOf(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "wifi-password")) == "old" &&
          Starts(ValueOf(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "wifi-password"), "enc3:") &&
          ValueOf(PICO_Path::FILE::CFG::SYS_NETWORK_CFG, "timezone") == "JST-9", "network.cfg");
    check(ValueOf(PICO_Path::FILE::CFG::SYS_CHAT_CFG, "server") == "https://chat.example" &&
          Dec("chat-token", ValueOf(PICO_Path::FILE::CFG::SYS_CHAT_CFG, "token")) == "tok123" &&
          Starts(ValueOf(PICO_Path::FILE::CFG::SYS_CHAT_CFG, "token"), "enc3:"), "chat.cfg(serverは平文のまま)");
    check(Dec("todoist-token", ValueOf(PICO_Path::FILE::CFG::SYS_TODOIST_CFG, "token")) == "0123456789abcdef0123456789abcdef01234567" &&
          Starts(ValueOf(PICO_Path::FILE::CFG::SYS_TODOIST_CFG, "token"), "enc3:"), "todoist.cfg(平文のトークンも)");
    check(Dec("calendar-url:family", ValueOf(PICO_Path::FILE::CALENDAR_SOURCES, "family")) == url &&
          Starts(ValueOf(PICO_Path::FILE::CALENDAR_SOURCES, "family"), "enc3:"), "カレンダーのURL");
    check(Starts(HostSd::files[PICO_Path::FILE::SSH_ID_ED25519], "enc3:"), "SSHの鍵が enc3: に");
    {
        uint8_t s2k[64], pub[32];
        check(SshUtil::LoadPrivateKey(PICO_Path::FILE::SSH_ID_ED25519, s2k, pub) == SshUtil::KeyResult::Ok &&
              memcmp(s2k, secret, 64) == 0, "SSHの鍵は同じ鍵に戻る");
    }
    check(HostSd::files.count(std::string(SYS_WIFI_CFG) + ".tmp") == 0, "一時ファイルが残らない");

    // 2回目は何もしない
    const MigrateResult again = MigrateSecrets();
    check(again.upgraded == 0 && again.failed == 0, "もう一度暗号化し直しても何も変わらない");
    check(!Provision(msg, sizeof(msg)), "2つ目は作れない");

    // ---- Luaの暗号 ----
    std::string lua_v3, out;
    SecretAead::Encrypt((const uint8_t*)"save", 4, "/lua/apps/x", nullptr, lua_v3);
    check(SecretAead::Decrypt(lua_v3.c_str(), lua_v3.size(), "/lua/apps/x", nullptr, out) == SecretAead::Result::Ok && out == "save",
          "Lua: 端末の鍵で暗号化して戻せる");
    check(SecretAead::Decrypt(lua_v1.c_str(), lua_v1.size(), "/lua/apps/x", nullptr, out) == SecretAead::Result::Ok && out == "save",
          "Lua: 鍵を作る前の暗号(version 1)も読める");
    check(SecretAead::Decrypt(lua_v3.c_str(), lua_v3.size(), "/lua/apps/y", nullptr, out) == SecretAead::Result::AuthFailed,
          "Lua: 別のアプリでは読めない");

    // ---- PIN: v1 を正しく打つと v2 へ ----
    LockFunctions::LockNow();
    check(LockFunctions::IsLocked(), "v1のPINでロックできる");
    check(LockFunctions::Submit("2580", 0) == LockFunctions::Check::Ok && !LockFunctions::IsLocked(), "v1のPINで解ける");
    check(LockFunctions::RecordUsesDeviceKey(LockFunctions::CurrentRecordForTest()), "解いたら v2 に書き直す");
    check(Starts(ValueOf(PICO_Path::FILE::CFG::SYS_LOCK_CFG, "pin"), "v2:"), "lock.cfg も v2");
    check(LockFunctions::VerifyRecord(LockFunctions::CurrentRecordForTest(), "2580") &&
          !LockFunctions::VerifyRecord(LockFunctions::CurrentRecordForTest(), "2581"), "v2 でも同じPINで通る");
    const std::string v2rec = LockFunctions::CurrentRecordForTest();

    // ---- 起動し直す ----
    DeviceKey::Clear();
    Setup();
    check(GetState() == State::Ready, "起動し直しても読める");
    check(Dec("wifi-ssid:1", ValueOf(SYS_WIFI_CFG, "ssid1")) == "HomeNet", "起動し直しても同じ鍵");
    check(SecretAead::Decrypt(lua_v3.c_str(), lua_v3.size(), "/lua/apps/x", nullptr, out) == SecretAead::Result::Ok, "Lua: 起動し直しても読める");

    // ---- 端末の鍵が無い本体(別の本体・OTPを読めない) ----
    DeviceKey::Clear();
    check(Dec("wifi-ssid:1", ValueOf(SYS_WIFI_CFG, "ssid1")) == "<fail>", "鍵が無いと enc3: は読めない");
    check(!PICO_Secret::NeedsUpgrade(ValueOf(SYS_WIFI_CFG, "ssid1").c_str()), "enc3: は書き直しの対象ではない");
    check(SecretAead::Decrypt(lua_v3.c_str(), lua_v3.size(), "/lua/apps/x", nullptr, out) == SecretAead::Result::NoDeviceKey,
          "Lua: 鍵が無ければ NoDeviceKey");
    LockFunctions::SetRecordForTest(v2rec.c_str());
    LockFunctions::LockNow();
    check(!LockFunctions::IsLocked(), "確かめられない v2 のPINではロックしない(二度と解けなくなるため)");
    check(!LockFunctions::VerifyRecord(v2rec.c_str(), "2580"), "鍵が無ければ v2 は通らない");

    // ---- 書きかけの枠 ----
    Test::ResetOtp();
    Test::PokeRaw(Test::BaseRow() + 5, 0x123456);   //1枠目に中身があるのに目印が無い
    Setup();
    check(GetState() == State::None, "1枠目が壊れていても None(2枠目がある)");
    check(Provision(msg, sizeof(msg)) && GetState() == State::Ready, "2枠目に作れる");
    DeviceKey::Clear();
    Setup();
    check(GetState() == State::Ready, "2枠目の鍵を起動時に読める");

    Test::ResetOtp();
    DeviceKey::Clear();
    Setup();
    Test::FailWriteAfter(5);
    check(!Provision(msg, sizeof(msg)) && !DeviceKey::available, "書き込みに失敗したら使わない");
    check(GetState() == State::None, "1枠目が書きかけでも2枠目がある");
    Test::FailWriteAfter(19);   //目印の1行だけ書けない
    check(!Provision(msg, sizeof(msg)), "2枠目も失敗");
    check(GetState() == State::Broken && !DeviceKey::available, "両方壊れたら Broken");
    check(!Provision(msg, sizeof(msg)), "Broken なら作れない");
    Test::FailWriteAfter(-1);

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
