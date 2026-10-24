// 保存済みのWi-Fiネットワーク(net/Wifi_Profiles)を検証するテスト。
//
// network.cfgの旧形式(wifi-ssid / wifi-password)からの取り込み、追加/更新/上限/削除、
// 接続できたものを先頭へ繰り上げること(=自動接続の順番)、ON/OFFの保存、
// SSID/パスワードが暗号化されてSDに平文で残らないことを確かめる。
// 後半はネットワークごとのIPの設定(固定IP/DNS。util/Net_Addr.hppの読み書きを含む)。
#include "net/Wifi_Profiles.hpp"
#include "storage/SD_Path.hpp"
#include "system/Config_Functions.hpp"
#include "crypto/Secret_Cipher.hpp"
#include "util/Net_Addr.hpp"
#include <cstdio>
#include <cstring>
#include <string>

// ログはここでは見ない(Config_Functions/Wifi_Profilesが出す警告の受け口だけ用意する)
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eqStr(const char* actual, const char* expected, const char* label){
    const bool ok = (strcmp(actual, expected) == 0);
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if(!ok){
        printf("       実測: [%s]\n", actual);
        printf("       期待: [%s]\n", expected);
        failures++;
    }
}

static const char* kWifi    = PICO_Path::FILE::CFG::SYS_WIFI_CFG;
static const char* kNetwork = PICO_Path::FILE::CFG::SYS_NETWORK_CFG;

static const char* SsidAt(int i){
    const WifiProfiles::Profile* p = WifiProfiles::At(i);
    return p ? p->ssid.c_str() : "(null)";
}

int main(){
    OSData::SD_usable = true;

    // ---- 旧形式(network.cfgの1組)からの取り込み ----
    {
        HostSd::files.clear();
        char enc_ssid[256], enc_pass[256];
        PICO_Secret::Encrypt("wifi-ssid", "home-ap", enc_ssid, sizeof(enc_ssid));
        PICO_Secret::Encrypt("wifi-password", "home-pass", enc_pass, sizeof(enc_pass));
        HostSd::files[kNetwork] = std::string("wifi-ssid=") + enc_ssid + "\nwifi-password=" + enc_pass
                                + "\nntp-server-1=ntp.nict.jp\n";

        check(WifiProfiles::Load(), "wifi.cfgが無ければnetwork.cfgから読める");
        check(WifiProfiles::Count() == 1, "旧形式の1組を取り込む");
        eqStr(SsidAt(0), "home-ap", "取り込んだSSID");
        eqStr(WifiProfiles::At(0)->password.c_str(), "home-pass", "取り込んだパスワード");
        check(WifiProfiles::IsEnabled(), "既定はON");
        check(HostSd::files.count(kWifi) == 1, "取り込んだ結果をwifi.cfgとして書く");

        // 一度wifi.cfgができたら、全部消してもnetwork.cfgの古いSSIDは復活しない
        WifiProfiles::Remove(0);
        check(WifiProfiles::Load(), "wifi.cfgを読み直せる");
        check(WifiProfiles::Count() == 0, "wifi.cfgがあれば旧形式は読まない(消したものが復活しない)");
    }

    // ---- 平文の旧形式・空のSSIDは取り込まない ----
    {
        HostSd::files.clear();
        HostSd::files[kNetwork] = "wifi-ssid=\nwifi-password=x\n";
        WifiProfiles::Load();
        check(WifiProfiles::Count() == 0, "SSIDが空なら取り込まない");

        HostSd::files.clear();
        HostSd::files[kNetwork] = "wifi-ssid=plain-ap\nwifi-password=\n";
        WifiProfiles::Load();
        check(WifiProfiles::Count() == 1 && strcmp(SsidAt(0), "plain-ap") == 0,
              "平文の旧形式・パスワード無し(オープン)も取り込む");
    }

    // ---- 追加・更新・上限 ----
    {
        HostSd::files.clear();
        WifiProfiles::Reset();
        int idx = -1;
        check(WifiProfiles::Put("a", "pa", &idx) == WifiProfiles::PutResult::Added && idx == 0, "1件目を追加");
        check(WifiProfiles::Put("b", "pb", &idx) == WifiProfiles::PutResult::Added && idx == 1, "新しいものは末尾へ");
        check(WifiProfiles::Put("a", "pa2", &idx) == WifiProfiles::PutResult::Updated && idx == 0,
              "同じSSIDはパスワードの書き換え(並びは変えない)");
        eqStr(WifiProfiles::At(0)->password.c_str(), "pa2", "書き換えたパスワード");
        check(WifiProfiles::Count() == 2, "更新では増えない");

        check(WifiProfiles::Put("", "x") == WifiProfiles::PutResult::Invalid, "空のSSIDは拒否");
        check(WifiProfiles::Put("123456789012345678901234567890123", "x") == WifiProfiles::PutResult::Invalid,
              "33バイトのSSIDは拒否");
        std::string long_pass(65, 'p');
        check(WifiProfiles::Put("c", long_pass.c_str()) == WifiProfiles::PutResult::Invalid, "65バイトのパスワードは拒否");
        std::string max_pass(64, 'p');
        check(WifiProfiles::Put("max-pass", max_pass.c_str()) == WifiProfiles::PutResult::Added, "64バイトのパスワードは通る");

        for(int i = WifiProfiles::Count(); i < WifiProfiles::kMaxProfiles; i++){
            char name[16];
            snprintf(name, sizeof(name), "n%d", i);
            WifiProfiles::Put(name, "");
        }
        check(WifiProfiles::Count() == WifiProfiles::kMaxProfiles, "上限まで入る");
        check(WifiProfiles::Put("overflow", "x") == WifiProfiles::PutResult::Full, "上限を超えると拒否");
        check(WifiProfiles::Put("a", "pa3") == WifiProfiles::PutResult::Updated, "満杯でも既存の更新はできる");

        // 保存した内容を読み直しても同じ(64バイトのパスワードが値の上限に収まること)
        check(WifiProfiles::Load(), "読み直せる");
        check(WifiProfiles::Count() == WifiProfiles::kMaxProfiles, "読み直しても件数が同じ");
        const int mp = WifiProfiles::Find("max-pass");
        check(mp >= 0 && WifiProfiles::At(mp)->password == max_pass.c_str(), "64バイトのパスワードの往復");
        eqStr(WifiProfiles::At(0)->password.c_str(), "pa3", "更新したパスワードの往復");
    }

    // ---- 接続できたものを先頭へ(自動接続の順番) ----
    {
        HostSd::files.clear();
        WifiProfiles::Reset();
        WifiProfiles::Put("a", "1");
        WifiProfiles::Put("b", "2");
        WifiProfiles::Put("c", "3");

        WifiProfiles::MarkConnected("c");
        eqStr(SsidAt(0), "c", "接続できたものが先頭");
        eqStr(SsidAt(1), "a", "残りは元の順で後ろへ");
        eqStr(SsidAt(2), "b", "残りは元の順で後ろへ(2)");
        eqStr(WifiProfiles::At(0)->password.c_str(), "3", "繰り上げてもパスワードは組のまま");

        const std::string before = HostSd::files[kWifi];
        WifiProfiles::MarkConnected("c");
        check(HostSd::files[kWifi] == before, "既に先頭なら書き直さない");
        WifiProfiles::MarkConnected("unknown");
        check(WifiProfiles::Count() == 3 && strcmp(SsidAt(0), "c") == 0, "一覧に無いSSIDでは何もしない");

        WifiProfiles::Load();
        eqStr(SsidAt(0), "c", "並びは保存される");
        eqStr(SsidAt(2), "b", "並びは保存される(末尾)");

        WifiProfiles::Remove(1);
        check(WifiProfiles::Count() == 2 && strcmp(SsidAt(1), "b") == 0, "削除すると後ろが詰まる");
        check(!WifiProfiles::Remove(5), "範囲外の削除はfalse");
    }

    // ---- 暗号化: SSID/パスワードの平文がSDに残らない。番号ごとに用途が違う ----
    {
        HostSd::files.clear();
        WifiProfiles::Reset();
        WifiProfiles::Put("same", "secret-pass");
        WifiProfiles::Put("same2", "secret-pass");
        const std::string& body = HostSd::files[kWifi];
        check(body.find("secret-pass") == std::string::npos, "パスワードの平文が残らない");
        check(body.find("same") == std::string::npos, "SSIDの平文が残らない");
        check(body.find("ssid1=enc1:") != std::string::npos && body.find("pass2=enc1:") != std::string::npos,
              "enc1:で保存される");

        // 同じパスワードでも番号が違えば暗号文が違う(XORで平文同士の関係が漏れない)
        std::string p1, p2;
        PICO_Config::ParseFile(kWifi, [&](const char* k, const char* v){
            if(strcmp(k, "pass1") == 0) p1 = v;
            if(strcmp(k, "pass2") == 0) p2 = v;
        });
        check(!p1.empty() && p1 != p2, "同じパスワードでも番号ごとに暗号文が違う");
    }

    // ---- ON/OFF・番号の抜け・壊れた行 ----
    {
        HostSd::files.clear();
        WifiProfiles::Reset();
        WifiProfiles::Put("x", "y");
        WifiProfiles::SetEnabled(false);
        WifiProfiles::Reset();
        WifiProfiles::Load();
        check(!WifiProfiles::IsEnabled(), "OFFは保存される");
        check(WifiProfiles::Count() == 1, "OFFでも一覧は残る");

        HostSd::files[kWifi] = "enabled=maybe\nssid3=c3\npass3=p3\nssid1=c1\nssid9=bad\nssid1x=bad\n";
        WifiProfiles::Load();
        check(WifiProfiles::IsEnabled(), "enabledが不正なら既定(ON)");
        check(WifiProfiles::Count() == 2, "番号の抜けは詰め、範囲外/不正なキーは無視");
        eqStr(SsidAt(0), "c1", "番号の若い順");
        eqStr(SsidAt(1), "c3", "番号の若い順(2)");
        eqStr(WifiProfiles::At(1)->password.c_str(), "p3", "番号で組になる");
    }

    // ---- アドレスの読み書き(util/Net_Addr.hpp) ----
    {
        uint32_t a = 0, m = 0;
        check(NetAddr::Parse(" 192.168.1.2 ", a) && a == 0xC0A80102u, "アドレスを読む(前後の空白は許す)");
        check(!NetAddr::Parse("192.168.1", a), "オクテットが足りなければ失敗");
        check(!NetAddr::Parse("192.168.1.256", a), "256以上は失敗");
        check(!NetAddr::Parse("192.168.1.2x", a), "後ろにゴミがあれば失敗");
        check(!NetAddr::Parse("1.2.3.0004", a), "4桁のオクテットは失敗");
        char buf[16];
        NetAddr::Format(0x0A000001u, buf, sizeof(buf));
        eqStr(buf, "10.0.0.1", "アドレスを書く");
        check(NetAddr::ParseCidr("192.168.1.50/24", a, m) && m == 0xFFFFFF00u, "/24");
        check(NetAddr::ParseCidr("10.1.2.3/255.255.0.0", a, m) && m == 0xFFFF0000u, "マスクで書いてもよい");
        check(NetAddr::ParseCidr("192.168.1.50", a, m) && m == 0xFFFFFF00u, "/を省くと/24");
        check(!NetAddr::ParseCidr("192.168.1.50/33", a, m), "/33は失敗");
        check(!NetAddr::ParseCidr("192.168.1.50/255.0.255.0", a, m), "連続していないマスクは失敗");
        check(NetAddr::MaskToPrefix(0xFFFFFFF0u) == 28 && NetAddr::MaskToPrefix(0xFF00FF00u) == -1, "マスク→長さ");
        uint32_t list[2] = {};
        int n = -1;
        check(NetAddr::ParseList("1.1.1.1, 8.8.8.8", list, 2, n) && n == 2 && list[1] == 0x08080808u, "カンマ区切り");
        check(NetAddr::ParseList("", list, 2, n) && n == 0, "空は0個");
        check(!NetAddr::ParseList("1.1.1.1,2.2.2.2,3.3.3.3", list, 2, n), "多すぎれば失敗");
        check(!NetAddr::IsValidHostInSubnet(0xC0A80100u, 0xFFFFFF00u), "ネットワーク部のアドレスは不可");
        check(!NetAddr::IsValidHostInSubnet(0xC0A801FFu, 0xFFFFFF00u), "ブロードキャストは不可");
        check(NetAddr::IsValidHostname("Pico-OS2") && !NetAddr::IsValidHostname("-pico")
              && !NetAddr::IsValidHostname("pico.os") && !NetAddr::IsValidHostname(""), "ホスト名");
    }

    // ---- ネットワークごとのIPの設定 ----
    {
        HostSd::files.clear();
        WifiProfiles::Reset();
        WifiProfiles::Put("home", "pw");
        WifiProfiles::Put("office", "pw2");

        WifiProfiles::IpConfig c;
        c.static_ip = true;
        NetAddr::ParseCidr("192.168.1.50/24", c.ip, c.mask);
        NetAddr::Parse("192.168.1.1", c.gateway);
        NetAddr::Parse("1.1.1.1", c.dns[0]);
        check(WifiProfiles::SetIpConfig(1, c), "固定IPを保存できる");

        WifiProfiles::IpConfig bad = c;
        NetAddr::Parse("10.0.0.1", bad.gateway);
        check(!WifiProfiles::SetIpConfig(1, bad), "別のサブネットのゲートウェイは断る");
        bad = c; bad.ip = 0xC0A801FFu;
        check(!WifiProfiles::SetIpConfig(1, bad), "ブロードキャストのアドレスは断る");
        check(!WifiProfiles::SetIpConfig(5, c), "範囲外は断る");

        const std::string& f = HostSd::files[kWifi];
        check(f.find("ip2=192.168.1.50/24\n") != std::string::npos, "ip2 として書く");
        check(f.find("gw2=192.168.1.1\n") != std::string::npos, "gw2 として書く");
        check(f.find("dns2=1.1.1.1\n") != std::string::npos, "dns2 として書く");
        check(f.find("ip1=") == std::string::npos, "DHCPのものは書かない");

        // 接続して先頭へ繰り上がっても、IPの設定はネットワークについていく
        WifiProfiles::MarkConnected("office");
        WifiProfiles::Load();
        const WifiProfiles::Profile* p = WifiProfiles::At(0);
        check(p && p->ssid == "office" && p->ip.static_ip && p->ip.ip == c.ip && p->ip.mask == c.mask
              && p->ip.gateway == c.gateway && p->ip.dns[0] == c.dns[0], "繰り上げ・読み直しでもIPの設定が残る");
        check(WifiProfiles::At(1)->ip.isDefault(), "もう片方はDHCPのまま");

        // DNSだけ(DHCPのまま)。2つ目だけ入れたら1つ目へ詰める
        WifiProfiles::IpConfig d;
        NetAddr::Parse("9.9.9.9", d.dns[1]);
        check(WifiProfiles::SetIpConfig(1, d), "DNSだけの指定");
        WifiProfiles::Load();
        check(!WifiProfiles::At(1)->ip.static_ip && WifiProfiles::At(1)->ip.dns[0] == 0x09090909u
              && WifiProfiles::At(1)->ip.dns[1] == 0, "DNSだけは詰めて保存し、読み直せる");

        // 削除で後ろが詰まっても組は崩れない。消した枠のIPの設定は残らない
        WifiProfiles::Remove(0);
        check(WifiProfiles::Count() == 1 && WifiProfiles::At(0)->ip.dns[0] == 0x09090909u, "削除で詰めても設定がついていく");
        WifiProfiles::Put("new", "");
        check(WifiProfiles::At(1)->ip.isDefault(), "新しく足した組はDHCP");

        // 手で壊した設定はDHCPへ戻す(繋がらなくなるより良い)
        HostSd::files[kWifi] = "ssid1=a\nip1=192.168.1.50/24\ngw1=10.0.0.1\nssid2=b\nip2=nonsense\ndns2=1.1.1.1,x\n";
        WifiProfiles::Load();
        check(WifiProfiles::Count() == 2 && WifiProfiles::At(0)->ip.isDefault(), "サブネットの合わない手書きはDHCPへ");
        check(WifiProfiles::At(1)->ip.isDefault(), "読めない値は無視");
    }

    printf("\n%s (失敗 %d 件)\n", failures == 0 ? "全て成功" : "失敗あり", failures);
    return failures == 0 ? 0 : 1;
}
