#include "functions/Lock_Functions.hpp"
#include "functions/KeyInput_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/widgets/systems/LockScreen.hpp"
#include "storage/SD_Path.hpp"
#include "util/Secret_Cipher.hpp"
#include "util/Secret_Aead.hpp"
#include "monocypher.h"
#include "OS_Data.hpp"

#include "Arduino.h"
#include <cstdio>
#include <cstring>

namespace {
    using LockFunctions::Step;
    using LockFunctions::Check;

    constexpr const char* kPrefix = "v1:";
    constexpr size_t kRecordLen = 3 + 32 + 1 + 64;   // "v1:" + 塩 + ":" + ハッシュ

    char record[kRecordLen + 1] = "";
    bool locked = false;
    Step step = Step::None;
    bool removing = false;          // Verifyの後にPINを外す(falseなら新しいPINへ)
    char new_pin[LockFunctions::kMaxPin + 1] = "";
    char message[64] = "";
    bool message_error = false;
    int fails = 0;
    unsigned long wait_until = 0;
    unsigned long wait_len = 0;
    bool was_sleeping = false;
    LockScreen* screen = nullptr;

    void SetMessage(const char* m, bool error){
        snprintf(message, sizeof(message), "%s", m);
        message_error = error;
        if(screen) screen->onStateChanged();
    }

    void Show(Step s){
        step = s;
        if(screen){
            screen->reset();
            screen->setVisible(true);
            screen->onStateChanged();
        }
    }

    void Hide(){
        step = Step::None;
        removing = false;
        memset(new_pin, 0, sizeof(new_pin));
        if(screen){
            screen->reset();
            screen->setVisible(false);
            //半透明のダイアログやキーボードの下まで描き直す(ロック画面の跡が残らないように)
            PICO_GFX::MarkDirtyBelow({0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
        }
    }

    void ToHex(const uint8_t* b, size_t n, char* out){
        static const char* h = "0123456789abcdef";
        for(size_t i = 0; i < n; i++){ out[i * 2] = h[b[i] >> 4]; out[i * 2 + 1] = h[b[i] & 15]; }
        out[n * 2] = '\0';
    }
    bool FromHex(const char* s, uint8_t* out, size_t n){
        for(size_t i = 0; i < n; i++){
            int v = 0;
            for(int k = 0; k < 2; k++){
                const char c = s[i * 2 + k];
                int d;
                if(c >= '0' && c <= '9') d = c - '0';
                else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else return false;
                v = v * 16 + d;
            }
            out[i] = (uint8_t)v;
        }
        return true;
    }

    bool SaveRecord(const char* rec){
        if(!OSData::SD_usable) return false;
        return PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_LOCK_CFG, "pin", rec);
    }

    // 間違えた回数で待たせる
    void Failed(unsigned long now){
        fails++;
        if(fails >= LockFunctions::kFreeTries){
            wait_len = (wait_len == 0) ? LockFunctions::kFirstWaitMs : wait_len * 2;
            if(wait_len > LockFunctions::kMaxWaitMs) wait_len = LockFunctions::kMaxWaitMs;
            wait_until = now + wait_len;
        }
    }

    Check VerifyThrottled(const char* pin, unsigned long now){
        if(LockFunctions::WaitRemainingMs(now) > 0) return Check::Wait;
        if(LockFunctions::VerifyRecord(record, pin)){
            fails = 0;
            wait_len = 0;
            wait_until = 0;
            return Check::Ok;
        }
        Failed(now);
        return Check::Wrong;
    }

    void WrongMessage(unsigned long now){
        char m[64];
        const unsigned long w = LockFunctions::WaitRemainingMs(now);
        if(w > 0) snprintf(m, sizeof(m), "違います。%lu秒待ってください", (w + 999) / 1000);
        else if(fails >= LockFunctions::kFreeTries - 2) snprintf(m, sizeof(m), "違います(あと%d回で待ち時間)", LockFunctions::kFreeTries - fails);
        else snprintf(m, sizeof(m), "PINが違います");
        SetMessage(m, true);
    }
}

bool LockFunctions::IsValidPin(const char* pin){
    if(!pin) return false;
    const size_t n = strlen(pin);
    if((int)n < kMinPin || (int)n > kMaxPin) return false;
    for(size_t i = 0; i < n; i++) if(pin[i] < '0' || pin[i] > '9') return false;
    return true;
}

void LockFunctions::HashPin(const char* pin, const uint8_t salt[16], uint8_t out[32]){
    uint8_t key[16];
    for(int i = 0; i < 4; i++){
        const uint32_t k = PICO_Secret::kKey[i];
        key[i * 4 + 0] = (uint8_t)k; key[i * 4 + 1] = (uint8_t)(k >> 8);
        key[i * 4 + 2] = (uint8_t)(k >> 16); key[i * 4 + 3] = (uint8_t)(k >> 24);
    }
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, 32, key, sizeof(key));
    static const uint8_t tag[] = "pico-os-pin";
    crypto_blake2b_update(&ctx, tag, sizeof(tag));
    crypto_blake2b_update(&ctx, salt, 16);
    crypto_blake2b_update(&ctx, (const uint8_t*)pin, strlen(pin));
    crypto_blake2b_final(&ctx, out);
    crypto_wipe(key, sizeof(key));
}

bool LockFunctions::FormatRecord(const char* pin, const uint8_t salt[16], char* out, size_t cap){
    if(!IsValidPin(pin) || cap < kRecordLen + 1) return false;
    uint8_t h[32];
    HashPin(pin, salt, h);
    char sh[33], hh[65];
    ToHex(salt, 16, sh);
    ToHex(h, 32, hh);
    snprintf(out, cap, "%s%s:%s", kPrefix, sh, hh);
    crypto_wipe(h, sizeof(h));
    return true;
}

bool LockFunctions::VerifyRecord(const char* rec, const char* pin){
    if(!rec || strlen(rec) != kRecordLen || strncmp(rec, kPrefix, 3) != 0 || rec[35] != ':') return false;
    if(!IsValidPin(pin)) return false;
    uint8_t salt[16], want[32], got[32];
    if(!FromHex(rec + 3, salt, 16) || !FromHex(rec + 36, want, 32)) return false;
    HashPin(pin, salt, got);
    const bool ok = crypto_verify32(got, want) == 0;
    crypto_wipe(got, sizeof(got));
    return ok;
}

void LockFunctions::SetRecordForTest(const char* rec){
    snprintf(record, sizeof(record), "%s", rec ? rec : "");
    fails = 0; wait_len = 0; wait_until = 0;
}

bool LockFunctions::HasPin(){ return record[0] != '\0'; }
bool LockFunctions::IsLocked(){ return locked; }
bool LockFunctions::IsShowing(){ return step != Step::None; }
LockFunctions::Step LockFunctions::CurrentStep(){ return step; }
const char* LockFunctions::Message(){ return message; }
bool LockFunctions::MessageIsError(){ return message_error; }

unsigned long LockFunctions::WaitRemainingMs(unsigned long now){
    if(wait_until == 0) return 0;
    const long d = (long)(wait_until - now);
    return d > 0 ? (unsigned long)d : 0;
}

void LockFunctions::LockNow(){
    if(!HasPin()) return;
    locked = true;
    removing = false;
    SetMessage("PINを入力", false);
    Show(Step::Unlock);
}

void LockFunctions::BeginChange(){
    if(locked) return;
    removing = false;
    if(HasPin()){
        SetMessage("今のPINを入力", false);
        Show(Step::Verify);
    }else{
        SetMessage("新しいPIN(数字4〜8桁)", false);
        Show(Step::NewPin);
    }
}

void LockFunctions::BeginRemove(){
    if(locked || !HasPin()) return;
    removing = true;
    SetMessage("PINを外すには今のPINを入力", false);
    Show(Step::Verify);
}

void LockFunctions::Cancel(){
    if(locked) return;
    Hide();
}

Check LockFunctions::Submit(const char* pin, unsigned long now){
    switch(step){
        case Step::Unlock: {
            const Check c = VerifyThrottled(pin, now);
            if(c == Check::Ok){
                locked = false;
                LOG_SYS_MSG("画面ロックを解きました");
                Hide();
            }else{
                WrongMessage(now);
            }
            return c;
        }
        case Step::Verify: {
            const Check c = VerifyThrottled(pin, now);
            if(c != Check::Ok){ WrongMessage(now); return c; }
            if(removing){
                record[0] = '\0';
                SaveRecord("");
                LOG_SYS_MSG("画面ロックのPINを外しました");
                Hide();
            }else{
                SetMessage("新しいPIN(数字4〜8桁)", false);
                step = Step::NewPin;
                if(screen) screen->reset();
            }
            return c;
        }
        case Step::NewPin:
            if(!IsValidPin(pin)){ SetMessage("数字4〜8桁にしてください", true); return Check::Invalid; }
            snprintf(new_pin, sizeof(new_pin), "%s", pin);
            SetMessage("もう一度入力", false);
            step = Step::Confirm;
            if(screen) screen->reset();
            return Check::Ok;
        case Step::Confirm: {
            if(strcmp(pin, new_pin) != 0){
                memset(new_pin, 0, sizeof(new_pin));
                SetMessage("一致しません。新しいPINから", true);
                step = Step::NewPin;
                if(screen) screen->reset();
                return Check::Wrong;
            }
            uint8_t salt[16];
            SecretAead::Random(salt, sizeof(salt));
            char rec[kRecordLen + 1];
            FormatRecord(new_pin, salt, rec, sizeof(rec));
            snprintf(record, sizeof(record), "%s", rec);
            SaveRecord(rec);
            LOG_SYS_MSG("画面ロックのPINを決めました");
            Hide();
            return Check::Ok;
        }
        case Step::None:
        default:
            return Check::Invalid;
    }
}

bool LockFunctions::HandleKey(const KeyInputFunctions::Event& ev){
    if(!IsShowing()) return false;
    if(screen) screen->onKey(ev);
    return true;   //ロック画面を出している間は全部取る
}

void LockFunctions::Setup(){
    record[0] = '\0';
    //無いのが普通(PIN無し)。ParseFile()は開けないとFAILを出すので先に確かめる
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_LOCK_CFG)) PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_LOCK_CFG, [](const char* key, const char* value){
        if(strcmp(key, "pin") == 0 && strlen(value) == kRecordLen) snprintf(record, sizeof(record), "%s", value);
    });
    screen = new LockScreen();
    screen->setVisible(false);
    WidgetFunctions::AddOverlay(screen);   //全オーバーレイの一番上(トーストやキーボードより上)
    KeyInputFunctions::intercept = &LockFunctions::HandleKey;
    if(HasPin()){
        LockNow();
        LOG_SYS_OK("Lock Setup: PINでロックしました");
    }
}

void LockFunctions::Update(){
    //スリープに入ったらロック(起きたときにはロック画面)
    const bool sleeping = PowerFunctions::IsSleeping();
    if(sleeping && !was_sleeping && HasPin() && !locked){
        //変更の途中ならやめる
        if(IsShowing()) Hide();
        LockNow();
    }
    was_sleeping = sleeping;

    if(IsShowing()){
        PadFunctions::SuppressThisFrame();
        if(screen) screen->tick();
    }
}
