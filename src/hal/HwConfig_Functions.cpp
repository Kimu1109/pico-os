#include "hal/HwConfig_Functions.hpp"

#include <cstdio>
#include <cstring>

#include "storage/Eeprom_Area.hpp"
#include "system/I18n_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/Notification_Functions.hpp"

using HwConfigCodec::Group;

namespace {
    // Load() で使えない組み合わせだったときの理由("hwcfg show" と通知で見せる)
    char invalid_reason[96] = "";
    // 書き出しの作業場所(20キー×20バイト程度。4KBまでのセクタの先頭だけ使う)
    uint8_t encode_buf[512];

    const char* SourceText(HwConfigFunctions::Source s){
        switch(s){
            case HwConfigFunctions::Source::Default: return "保存なし(既定で起動)";
            case HwConfigFunctions::Source::Stored:  return "保存した値で起動";
            case HwConfigFunctions::Source::Broken:  return "保存した値が壊れていたので既定で起動";
            case HwConfigFunctions::Source::Invalid: return "保存した値が使えないので既定で起動";
        }
        return "";
    }

    void PrintHelp(){
        LOG_SYS_MSG("[HWCFG] 液晶・SDの配線(内蔵フラッシュ。反映は再起動後)");
        LOG_SYS_MSG("[HWCFG] hwcfg show|keys|set <キー> <値>|unset <キー>|save|discard|reset|reboot");
    }

    void PrintKeys(){
        for(size_t i = 0; i < HwConfigCodec::FieldCount(); i++){
            const HwConfigCodec::Field& f = HwConfigCodec::FieldAt(i);
            LOG_SYS_MSG("[HWCFG] %-13s %s", f.key, f.help);
        }
    }

    void PrintShow(){
        using namespace HwConfigFunctions;
        LOG_SYS_MSG("[HWCFG] 状態: %s", SourceText(source));
        if(source == Source::Invalid && invalid_reason[0]) LOG_SYS_MSG("[HWCFG]   理由: %s", invalid_reason);
        if(lcd_fell_back) LOG_SYS_WARN("[HWCFG] 液晶・タッチ: 保存した値で応答が無かったので既定で起動しています");
        if(sd_fell_back) LOG_SYS_WARN("[HWCFG] SD: 保存した値で開けなかったので既定で起動しています");
        LOG_SYS_MSG("[HWCFG] キー          動作中  編集中  既定"); // 全角は端末で2桁なので手で揃える
        const HwConfig def = HwConfigDefault();
        bool dirty = false;
        for(size_t i = 0; i < HwConfigCodec::FieldCount(); i++){
            const HwConfigCodec::Field& f = HwConfigCodec::FieldAt(i);
            char a[16], e[16], d[16];
            HwConfigCodec::FormatValue(f.kind, HwConfigCodec::Get(active, i), a, sizeof(a));
            HwConfigCodec::FormatValue(f.kind, HwConfigCodec::Get(edit, i), e, sizeof(e));
            HwConfigCodec::FormatValue(f.kind, HwConfigCodec::Get(def, i), d, sizeof(d));
            const bool changed = HwConfigCodec::Get(edit, i) != HwConfigCodec::Get(stored, i);
            if(changed) dirty = true;
            LOG_SYS_MSG("[HWCFG] %-13s %-7s %-7s %s%s", f.key, a, e, d, changed ? "  (未保存)" : "");
        }
        if(dirty) LOG_SYS_MSG("[HWCFG] 未保存の変更があります(hwcfg save で保存)");
    }

    // "set lcd.cs 17" の "lcd.cs 17" を2つに分ける
    bool SplitKeyValue(const char* s, char* key, size_t key_cap, const char** value){
        while(*s == ' ') s++;
        size_t n = 0;
        while(s[n] && s[n] != ' ' && s[n] != '=') n++;
        if(n == 0 || n >= key_cap) return false;
        memcpy(key, s, n);
        key[n] = '\0';
        s += n;
        while(*s == ' ' || *s == '=') s++;
        *value = s;
        return true;
    }

    // "set" などのあとの空白を飛ばす。語が一致しなければ nullptr
    const char* Word(const char* cmd, const char* word){
        const size_t n = strlen(word);
        if(strncmp(cmd, word, n) != 0 || (cmd[n] != ' ' && cmd[n] != '\0')) return nullptr;
        cmd += n;
        while(*cmd == ' ') cmd++;
        return cmd;
    }

    void TrimRight(char* s){
        size_t n = strlen(s);
        while(n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
    }
}

void HwConfigFunctions::Load(){
    const HwConfig def = HwConfigDefault();
    const uint8_t* data = EepromArea::Data();
    HwConfig decoded = def;
    const HwConfigCodec::DecodeResult r = HwConfigCodec::Decode(data, data ? EepromArea::kBytes : 0, decoded);
    invalid_reason[0] = '\0';
    lcd_fell_back = false;
    sd_fell_back = false;

    switch(r){
        case HwConfigCodec::DecodeResult::Empty:
            source = Source::Default;
            stored = def;
            break;
        case HwConfigCodec::DecodeResult::Broken:
            source = Source::Broken;
            stored = def;
            LOG_SYS_WARN("液晶・SDの設定(内蔵フラッシュ)が壊れているので既定で起動します");
            break;
        case HwConfigCodec::DecodeResult::Ok:
            stored = decoded;
            if(HwConfigCodec::Validate(decoded, invalid_reason, sizeof(invalid_reason))){
                source = Source::Stored;
            }else{
                source = Source::Invalid;
                LOG_SYS_WARN("液晶・SDの設定(内蔵フラッシュ)が使えないので既定で起動します: %s", invalid_reason);
            }
            break;
    }
    active = source == Source::Stored ? stored : def;
    // 使えない値だったときも、直しやすいように読めた値を編集の出発点にする
    edit = stored;
    if(source == Source::Stored && !HwConfigCodec::SameGroup(stored, def, Group::Lcd)) LOG_SYS_MSG("液晶・タッチ: 内蔵フラッシュの設定で起動します");
    if(source == Source::Stored && !HwConfigCodec::SameGroup(stored, def, Group::Sd)) LOG_SYS_MSG("SD: 内蔵フラッシュの設定で起動します");
}

bool HwConfigFunctions::CanFallBack(Group g){
    return source == Source::Stored && !HwConfigCodec::SameGroup(active, HwConfigDefault(), g);
}

HwConfig HwConfigFunctions::WithDefault(Group g){
    HwConfig c = active;
    HwConfigCodec::CopyGroup(c, HwConfigDefault(), g);
    return c;
}

void HwConfigFunctions::MarkFellBack(Group g, const HwConfig& used){
    HwConfigCodec::CopyGroup(active, used, g);
    if(g == Group::Lcd) lcd_fell_back = true;
    else sd_fell_back = true;
}

bool HwConfigFunctions::Save(const HwConfig& c, char* why, size_t cap){
    if(!HwConfigCodec::Validate(c, why, cap)) return false;
    const size_t n = HwConfigCodec::Encode(c, encode_buf, sizeof(encode_buf));
    if(n == 0){
        if(why && cap) snprintf(why, cap, "書き出しが作業場所に入りません");
        return false;
    }
    if(!EepromArea::Write(encode_buf, n)){
        if(why && cap) snprintf(why, cap, "内蔵フラッシュへの書き込みに失敗しました");
        return false;
    }
    stored = c;
    return true;
}

bool HwConfigFunctions::Erase(){
    if(!EepromArea::Erase()) return false;
    stored = HwConfigDefault();
    return true;
}

bool HwConfigFunctions::FeedSerialLine(const char* line){
    if(!line || strncmp(line, "hwcfg", 5) != 0 || (line[5] != ' ' && line[5] != '\0')) return false;
    const char* cmd = line + 5;
    while(*cmd == ' ') cmd++;

    const char* rest = nullptr;
    if(*cmd == '\0' || Word(cmd, "help")) PrintHelp();
    else if(Word(cmd, "show")) PrintShow();
    else if(Word(cmd, "keys")) PrintKeys();
    else if((rest = Word(cmd, "set"))){
        char key[24];
        const char* value = nullptr;
        if(!SplitKeyValue(rest, key, sizeof(key), &value) || !*value){
            LOG_SYS_WARN("[HWCFG] 使い方: hwcfg set <キー> <値>");
            return true;
        }
        char v[24];
        snprintf(v, sizeof(v), "%s", value);
        TrimRight(v);
        const char* why = nullptr;
        if(HwConfigCodec::Set(edit, key, v, &why)) LOG_SYS_OK("[HWCFG] %s = %s(hwcfg save で保存)", key, v);
        else LOG_SYS_WARN("[HWCFG] %s: %s(hwcfg keys で一覧)", key, why ? why : "");
    }
    else if((rest = Word(cmd, "unset"))){
        char key[24];
        snprintf(key, sizeof(key), "%s", rest);
        TrimRight(key);
        const int i = HwConfigCodec::Find(key);
        if(i < 0){
            LOG_SYS_WARN("[HWCFG] 知らないキーです: %s", key);
            return true;
        }
        HwConfigCodec::Put(edit, (size_t)i, HwConfigCodec::Get(HwConfigDefault(), (size_t)i));
        LOG_SYS_OK("[HWCFG] %s を既定へ戻しました(hwcfg save で保存)", key);
    }
    else if(Word(cmd, "save")){
        char why[128];
        if(Save(edit, why, sizeof(why))){
            LOG_SYS_OK("[HWCFG] 保存しました。hwcfg reboot で反映します");
        }else{
            LOG_SYS_FAIL("[HWCFG] 保存できません: %s", why);
        }
    }
    else if(Word(cmd, "discard")){
        edit = stored;
        LOG_SYS_OK("[HWCFG] 編集を捨てました");
    }
    else if(Word(cmd, "reset")){
        if(Erase()){
            edit = stored;
            LOG_SYS_OK("[HWCFG] 保存を消しました。hwcfg reboot で既定に戻ります");
        }else{
            LOG_SYS_FAIL("[HWCFG] 内蔵フラッシュを消せませんでした");
        }
    }
    else if(Word(cmd, "reboot")){
        if(reboot){
            LOG_SYS_MSG("[HWCFG] 再起動します");
            reboot();
        }else{
            LOG_SYS_WARN("[HWCFG] このビルドでは再起動できません");
        }
    }
    else{
        LOG_SYS_WARN("[HWCFG] 知らないコマンドです: %s", cmd);
        PrintHelp();
    }
    return true;
}

void HwConfigFunctions::PostPendingNotice(){
    const char* body = nullptr;
    if(lcd_fell_back && sd_fell_back) body = TR("保存した液晶とSDの設定が使えなかったので、既定の設定で起動しました");
    else if(lcd_fell_back) body = TR("保存した液晶の設定で応答が無かったので、既定の設定で起動しました");
    else if(sd_fell_back) body = TR("保存したSDの設定で開けなかったので、既定の設定で起動しました");
    else if(source == Source::Broken || source == Source::Invalid) body = TR("保存した液晶・SDの設定が読めなかったので、既定の設定で起動しました");
    if(!body) return;

    NotificationFunctions::Content c;
    NotificationFunctions::Sanitize(c.title, TR("ハードウェアの設定"));
    NotificationFunctions::Sanitize(c.body, body);
    c.sound = false;
    NotificationFunctions::Post(c);
}
