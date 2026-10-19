// 画面ロック(LockFunctions / LockScreen)。PINの記録の作り方と確かめ方、ロック画面の流れ(解く・決める・変える・外す)、
// 間違えたときの待ち時間、打鍵とタッチ。
#include "services/Lock_Functions.hpp"
#include "system/KeyInput_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "gui/widgets/systems/LockScreen.hpp"
#include "storage/SD_Path.hpp"
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
static bool fake_sleeping = false;
bool PowerFunctions::IsSleeping(){ return fake_sleeping; }
static int suppress_calls = 0;
void PadFunctions::SuppressThisFrame(){ suppress_calls++; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static LockScreen* Screen(){
    for(Widget* w : WidgetFunctions::overlays) if(w->getWidgetType() == WidgetType::LockScreen) return static_cast<LockScreen*>(w);
    return nullptr;
}

static void TypePin(LockScreen* s, const char* pin){
    for(const char* p = pin; *p; p++) s->pressKey(*p == '0' ? LockScreen::kKeyZero : (*p - '1'));
    s->pressKey(LockScreen::kKeyOk);
}

int main(){
    using namespace LockFunctions;
    HostSd::files.clear();
    OSData::SD_usable = true;

    // ---- 記録 ----
    const uint8_t salt[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    char rec[128];
    check(FormatRecord("1234", salt, rec, sizeof(rec)), "記録を作れる");
    check(strncmp(rec, "v1:", 3) == 0 && strlen(rec) == 3 + 32 + 1 + 64, "記録の書式");
    check(strstr(rec, "1234") == nullptr, "PINそのものは入らない");
    check(VerifyRecord(rec, "1234"), "正しいPINは通る");
    check(!VerifyRecord(rec, "1235") && !VerifyRecord(rec, "12345"), "違うPINは通らない");
    char rec2[128];
    const uint8_t salt2[16] = {9};
    FormatRecord("1234", salt2, rec2, sizeof(rec2));
    check(strcmp(rec, rec2) != 0, "塩が違えば記録も違う");
    check(!FormatRecord("123", salt, rec, sizeof(rec)) && !FormatRecord("123456789", salt, rec, sizeof(rec)) && !FormatRecord("12a4", salt, rec, sizeof(rec)), "4〜8桁の数字だけ");
    check(!VerifyRecord("v1:zz", "1234") && !VerifyRecord("", "1234"), "壊れた記録は通らない");

    // ---- PIN無しで起動: ロックしない ----
    Setup();
    LockScreen* s = Screen();
    check(s != nullptr, "ロック画面のオーバーレイを作る");
    check(!HasPin() && !IsLocked() && !IsShowing() && !s->getVisible(), "PIN無しならロックしない");
    Update();
    check(suppress_calls == 0, "ロックしていなければコントローラーを止めない");

    // ---- PINを決める ----
    BeginChange();
    check(CurrentStep() == Step::NewPin && s->getVisible(), "PIN無しなら新しいPINから聞く");
    TypePin(s, "12");
    check(CurrentStep() == Step::NewPin && MessageIsError(), "短いPINは断る");
    TypePin(s, "2468");
    check(CurrentStep() == Step::Confirm, "もう一度聞く");
    TypePin(s, "2469");
    check(CurrentStep() == Step::NewPin && MessageIsError(), "一致しなければ新しいPINからやり直し");
    TypePin(s, "2468");
    TypePin(s, "2468");
    check(HasPin() && !IsShowing() && !s->getVisible(), "決まると閉じる");
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_LOCK_CFG].find("pin=v1:") != std::string::npos, "lock.cfgへ保存する");

    // ---- スリープでロック ----
    fake_sleeping = true;
    Update();
    check(IsLocked() && CurrentStep() == Step::Unlock && s->getVisible(), "スリープに入るとロック");
    check(suppress_calls > 0, "ロック中はコントローラーを後ろへ渡さない");
    fake_sleeping = false;
    Update();
    s->pressKey(LockScreen::kKeyBack);
    check(IsLocked() && IsShowing(), "ロックを解く途中は取消できない");

    // ---- 打鍵 ----
    KeyInputFunctions::Event ev;
    ev.key = KeyInputFunctions::Key::Char;
    for(const char* p = "2468"; *p; p++){ ev.cp = (uint32_t)*p; check(KeyInputFunctions::intercept && KeyInputFunctions::intercept(ev), "ロック中の打鍵はロック画面が取る"); }
    check(s->enteredLength() == 4, "打鍵で桁が入る");
    ev.key = KeyInputFunctions::Key::Enter;
    KeyInputFunctions::intercept(ev);
    check(!IsLocked() && !IsShowing(), "正しいPINで解ける");
    ev.key = KeyInputFunctions::Key::Char; ev.cp = '1';
    check(!KeyInputFunctions::intercept(ev), "解けたら打鍵は取らない");

    // ---- 間違え続けると待たせる ----
    LockNow();
    unsigned long now = 100000;
    for(int i = 0; i < kFreeTries; i++){
        const Check c = Submit("0000", now);
        check(c == Check::Wrong, "違う");
    }
    check(WaitRemainingMs(now) == kFirstWaitMs, "5回目で30秒待たせる");
    check(Submit("2468", now + 1000) == Check::Wait && IsLocked(), "待っている間は正しくても通らない");
    check(Submit("0000", now + kFirstWaitMs) == Check::Wrong && WaitRemainingMs(now + kFirstWaitMs) == kFirstWaitMs * 2, "さらに間違えると倍");
    check(Submit("2468", now + kFirstWaitMs * 3) == Check::Ok && !IsLocked(), "待った後なら通る");
    check(WaitRemainingMs(now + kFirstWaitMs * 3) == 0, "通ったら数え直す");

    // ---- タッチ ----
    LockNow();
    int x, y;
    // 「2」のキーの真ん中
    for(y = 0; y < SCREEN_HEIGHT && s->keyAt(120, y) != 1; y++){}
    check(y < SCREEN_HEIGHT, "「2」のキーがある");
    OSData::touchX = 120; OSData::touchY = (int16_t)(y + 10);
    s->causeOnPressStart(); s->causeOnPressEnd();
    check(s->enteredLength() == 1, "タップで桁が入る");
    OSData::touchX = 5; OSData::touchY = 5;
    s->causeOnPressStart(); s->causeOnPressEnd();
    check(s->enteredLength() == 1, "キーの外のタップは何もしない");
    check(s->hitTest(5, 5) && s->hitTest(230, 310), "画面のどこを触ってもロック画面が取る");
    s->reset();
    x = 0; (void)x;

    // ---- 鳴っていれば止められる ----
    static bool ringing = true;
    static int stopped = 0;
    ringing_query = [](){ return ringing; };
    ringing_stop = [](){ stopped++; ringing = false; };
    s->tick();
    int ry = 0;
    for(ry = 0; ry < SCREEN_HEIGHT && s->keyAt(120, ry) != LockScreen::kKeyRing; ry++){}
    check(ry < SCREEN_HEIGHT, "鳴っていれば止めるボタンを出す");
    s->pressKey(LockScreen::kKeyRing);
    check(stopped == 1 && IsLocked(), "ロックを解かずに止められる");

    // ---- 変える・外す ----
    TypePin(s, "2468");
    BeginChange();
    check(CurrentStep() == Step::Verify, "変える前に今のPINを聞く");
    TypePin(s, "1111");
    check(CurrentStep() == Step::Verify && MessageIsError(), "今のPINが違えば進まない");
    TypePin(s, "2468");
    TypePin(s, "13579");
    TypePin(s, "13579");
    check(HasPin() && !IsShowing(), "新しいPINになる");
    LockNow();
    TypePin(s, "2468");
    check(IsLocked(), "古いPINでは解けない");
    TypePin(s, "13579");
    check(!IsLocked(), "新しいPINで解ける");
    BeginChange();
    s->pressKey(LockScreen::kKeyBack);
    check(!IsShowing(), "変更の途中は取消できる");
    BeginRemove();
    TypePin(s, "13579");
    check(!HasPin() && !IsShowing(), "外せる");
    check(HostSd::files[PICO_Path::FILE::CFG::SYS_LOCK_CFG].find("pin=v1:") == std::string::npos, "lock.cfgからも消える");
    LockNow();
    check(!IsLocked(), "PIN無しならロックしない");

    // ---- 起動時: PINがあればロック ----
    FormatRecord("1234", salt, rec, sizeof(rec));
    HostSd::files[PICO_Path::FILE::CFG::SYS_LOCK_CFG] = std::string("pin = ") + rec + "\n";
    { LockScreen* old = Screen(); WidgetFunctions::RemoveAny(old); delete old; }
    Setup();
    check(IsLocked() && Screen() && Screen()->getVisible(), "起動したらロック");

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
