#include "net/Wifi_Profiles.hpp"
#include "functions/Config_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "util/Secret_Cipher.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
    WifiProfiles::Profile profiles[WifiProfiles::kMaxProfiles];
    int  profile_count = 0;
    bool enabled = true;

    // 用途文字列: "wifi-ssid:1" / "wifi-password:1"(番号は1始まり、ファイル上のキーと揃える)
    void PurposeFor(char* out, size_t cap, bool is_ssid, int number){
        snprintf(out, cap, "%s:%d", is_ssid ? "wifi-ssid" : "wifi-password", number);
    }

    bool Valid(const char* ssid, const char* password){
        if(!ssid || ssid[0] == '\0') return false;
        if(strlen(ssid) > WifiProfiles::kMaxSsidBytes) return false;
        if(password && strlen(password) > WifiProfiles::kMaxPasswordBytes) return false;
        return true;
    }

    // "ssid12" → 12。接頭辞が違う/番号が範囲外なら0
    int KeyNumber(const char* key, const char* prefix){
        const size_t n = strlen(prefix);
        if(strncmp(key, prefix, n) != 0) return 0;
        const char* p = key + n;
        if(*p < '1' || *p > '9') return 0;
        char* end = nullptr;
        const long v = strtol(p, &end, 10);
        if(*end != '\0' || v < 1 || v > WifiProfiles::kMaxProfiles) return 0;
        return (int)v;
    }

    bool LoadLegacy(){
        // 1つだけ持てた頃の network.cfg(wifi-ssid / wifi-password。用途文字列は番号なし)
        FixedString<PICO_STR_M> ssid;
        FixedString<PICO_STR_L> password;
        const bool opened = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_NETWORK_CFG,
            [&](const char* key, const char* value){
                if(strcmp(key, "wifi-ssid") == 0){
                    char buf[PICO_STR_M];
                    if(PICO_Secret::Decrypt("wifi-ssid", value, buf, sizeof(buf))) ssid.assign(buf);
                }else if(strcmp(key, "wifi-password") == 0){
                    char buf[PICO_STR_L];
                    if(PICO_Secret::Decrypt("wifi-password", value, buf, sizeof(buf))) password.assign(buf);
                }
            }
        );
        if(!opened) return false;
        if(Valid(ssid.c_str(), password.c_str())){
            profiles[0].ssid.assign(ssid.c_str());
            profiles[0].password.assign(password.c_str());
            profile_count = 1;
            LOG_SYS_MSG("Wi-Fi: network.cfgのSSIDを保存済みネットワークへ取り込みました");
        }
        return true;
    }
}

void WifiProfiles::Reset(){
    for(int i = 0; i < kMaxProfiles; i++){
        profiles[i].ssid.clear();
        profiles[i].password.clear();
    }
    profile_count = 0;
    enabled = true;
}

bool WifiProfiles::Load(){
    Reset();

    if(!OSData::SD.exists(PICO_Path::FILE::CFG::SYS_WIFI_CFG)){
        if(!LoadLegacy()) return false;
        // 取り込んだ結果(空でも)をwifi.cfgとして残す。次からは旧形式を見ない
        // (全部消した後に、network.cfgに残った古いSSIDが復活しないように)
        Save();
        return true;
    }

    // 番号ごとに一旦置き、最後に番号順で詰める(抜けた番号があっても読めるように)
    Profile slots[kMaxProfiles];
    bool has_ssid[kMaxProfiles] = {};

    const bool opened = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_WIFI_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "enabled") == 0){
                bool b = true;
                if(PICO_Config::ConfigValue::AsBool(value, b)) enabled = b;
                else LOG_SYS_WARN("wifi.cfg: enabled は true/false です: %s", value);
                return;
            }
            char purpose[24];
            if(const int n = KeyNumber(key, "ssid")){
                char buf[PICO_STR_M];
                PurposeFor(purpose, sizeof(purpose), true, n);
                if(PICO_Secret::Decrypt(purpose, value, buf, sizeof(buf))){
                    slots[n - 1].ssid.assign(buf);
                    has_ssid[n - 1] = (buf[0] != '\0');
                }else{
                    LOG_SYS_WARN("wifi.cfg: %s を読めませんでした", key);
                }
            }else if(const int m = KeyNumber(key, "pass")){
                char buf[PICO_STR_L];
                PurposeFor(purpose, sizeof(purpose), false, m);
                if(PICO_Secret::Decrypt(purpose, value, buf, sizeof(buf))){
                    slots[m - 1].password.assign(buf);
                }else{
                    LOG_SYS_WARN("wifi.cfg: %s を読めませんでした", key);
                }
            }
        }
    );
    if(!opened) return false;

    for(int i = 0; i < kMaxProfiles; i++){
        if(!has_ssid[i]) continue;
        if(Find(slots[i].ssid.c_str()) >= 0) continue; // 同じSSIDが2つあれば若い番号を採る
        profiles[profile_count].ssid.assign(slots[i].ssid.c_str());
        profiles[profile_count].password.assign(slots[i].password.c_str());
        profile_count++;
    }
    return true;
}

bool WifiProfiles::Save(){
    const char* path = PICO_Path::FILE::CFG::SYS_WIFI_CFG;
    char tmp_path[PICO_PATH_LEN];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FsFile f = OSData::SD.open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){
        LOG_SYS_FAIL("Wi-Fi: %s を作れませんでした", tmp_path);
        return false;
    }

    bool ok = true;
    char line[PICO_Config::kConfigMaxLineLen];
    auto put = [&](const char* text){
        const size_t len = strlen(text);
        if(ok && f.write(text, len) != len) ok = false;
    };

    put("# 保存済みのWi-Fiネットワーク(番号の若い順が直近で接続した順)。設定アプリが書き換える\n");
    snprintf(line, sizeof(line), "enabled=%s\n", enabled ? "true" : "false");
    put(line);

    for(int i = 0; i < profile_count && ok; i++){
        char purpose[24];
        char enc[PICO_Config::kConfigMaxValueLen];

        PurposeFor(purpose, sizeof(purpose), true, i + 1);
        if(!PICO_Secret::Encrypt(purpose, profiles[i].ssid.c_str(), enc, sizeof(enc))){ ok = false; break; }
        snprintf(line, sizeof(line), "ssid%d=%s\n", i + 1, enc);
        put(line);

        PurposeFor(purpose, sizeof(purpose), false, i + 1);
        if(!PICO_Secret::Encrypt(purpose, profiles[i].password.c_str(), enc, sizeof(enc))){ ok = false; break; }
        snprintf(line, sizeof(line), "pass%d=%s\n", i + 1, enc);
        put(line);
    }
    f.close();

    if(!ok){
        LOG_SYS_FAIL("Wi-Fi: wifi.cfgの書き込みに失敗しました");
        OSData::SD.remove(tmp_path);
        return false;
    }
    if(OSData::SD.exists(path) && !OSData::SD.remove(path)){
        LOG_SYS_FAIL("Wi-Fi: 古いwifi.cfgを消せませんでした");
        OSData::SD.remove(tmp_path);
        return false;
    }
    if(!OSData::SD.rename(tmp_path, path)){
        LOG_SYS_FAIL("Wi-Fi: wifi.cfgを差し替えられませんでした");
        return false;
    }
    return true;
}

int WifiProfiles::Count(){ return profile_count; }

const WifiProfiles::Profile* WifiProfiles::At(int index){
    if(index < 0 || index >= profile_count) return nullptr;
    return &profiles[index];
}

int WifiProfiles::Find(const char* ssid){
    if(!ssid) return -1;
    for(int i = 0; i < profile_count; i++){
        if(profiles[i].ssid == ssid) return i;
    }
    return -1;
}

WifiProfiles::PutResult WifiProfiles::Put(const char* ssid, const char* password, int* out_index){
    if(out_index) *out_index = -1;
    if(!password) password = "";
    if(!Valid(ssid, password)) return PutResult::Invalid;

    int idx = Find(ssid);
    PutResult result = PutResult::Updated;
    if(idx < 0){
        if(profile_count >= kMaxProfiles) return PutResult::Full;
        idx = profile_count++;
        profiles[idx].ssid.assign(ssid);
        result = PutResult::Added;
    }
    profiles[idx].password.assign(password);
    Save();
    if(out_index) *out_index = idx;
    return result;
}

bool WifiProfiles::Remove(int index){
    if(index < 0 || index >= profile_count) return false;
    for(int i = index; i + 1 < profile_count; i++){
        profiles[i].ssid.assign(profiles[i + 1].ssid.c_str());
        profiles[i].password.assign(profiles[i + 1].password.c_str());
    }
    profile_count--;
    profiles[profile_count].ssid.clear();
    profiles[profile_count].password.clear();
    Save();
    return true;
}

void WifiProfiles::MarkConnected(const char* ssid){
    const int idx = Find(ssid);
    if(idx <= 0) return; // 一覧に無い / 既に先頭

    // 先頭へ繰り上げ、間の組を1つずつ後ろへずらす
    Profile moved;
    moved.ssid.assign(profiles[idx].ssid.c_str());
    moved.password.assign(profiles[idx].password.c_str());
    for(int i = idx; i > 0; i--){
        profiles[i].ssid.assign(profiles[i - 1].ssid.c_str());
        profiles[i].password.assign(profiles[i - 1].password.c_str());
    }
    profiles[0].ssid.assign(moved.ssid.c_str());
    profiles[0].password.assign(moved.password.c_str());
    Save();
}

bool WifiProfiles::IsEnabled(){ return enabled; }

void WifiProfiles::SetEnabled(bool value){
    if(enabled == value) return;
    enabled = value;
    Save();
}
