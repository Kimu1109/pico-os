// CardKB2のUARTモード(CardKbUart / CardKbFunctions の mode = uart)の検証。
//
// SerialPIOは stubs/SerialPIO.h(= pc/compat/SerialPIO.h)の PicoPcSerialPio::rx へバイトを積んで表し、時刻は直接渡す。
// 確かめること:
//   - フレーム: 押した/離した・チェックサム違い・途中から/ゴミ混じりでも次のAAで読み直す・1バイトずつ届いても同じ
//   - 文字: 通常/Aa(押している間・1回押し=次の英字だけ・2回押し=固定と解除)/Sym(押している間・1回押しで切り替え)
//   - Fn: 矢印・Esc・Delete・Tab・Ctrl+英字・割り当てのないキー / Shift付きの矢印
//   - 押しっぱなしの繰り返し(300ms後から50msごと、修飾キーは繰り返さない、長すぎれば止める)
//   - cardkb.cfg の mode = uart で SerialPIO から読み、I2Cの呼びかけはしない / 無ければI2C / 読めない値
#include "hal/CardKB_Functions.hpp"
#include "hal/CardKB_Uart.hpp"
#include "system/KeyInput_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <SdFat.h>
#include <SerialPIO.h>
#include <Wire.h>
#include <cstdio>
#include <string>

static int log_warn = 0;
void LogFunctions::Log(LogFunctions::LogType t, const char*, ...){
    if(t == LogFunctions::LogType::SYS_WARN) log_warn++;
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using KeyInputFunctions::Event;
using KeyInputFunctions::Key;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// ---- フレームを作って食わせる ----
static unsigned long now = 1000;

static void Frame(uint8_t key, bool pressed, std::string* out){
    const uint8_t st = pressed ? CardKbUart::kStatePressed : CardKbUart::kStateReleased;
    const uint8_t f[5] = {0xAA, 0x03, key, st, (uint8_t)(0x03 + key + st)};
    out->append((const char*)f, 5);
}
static void FeedStr(const std::string& s){
    for(unsigned char c : s) CardKbUart::Feed(c, now);
}
static void Press(uint8_t key){ std::string s; Frame(key, true, &s); FeedStr(s); CardKbUart::Tick(now); }
static void Release(uint8_t key){ std::string s; Frame(key, false, &s); FeedStr(s); CardKbUart::Tick(now); }
static void Tap(uint8_t key){ Press(key); now += 30; Release(key); now += 30; CardKbUart::Tick(now); }
static void Advance(unsigned long ms){
    for(unsigned long t = 0; t < ms; t += 10){ now += 10; CardKbUart::Tick(now); }
}

static void ClearQueue(){ Event ev; while(KeyInputFunctions::Pop(ev)){} }
// 列の文字をつなげて返す(文字以外は {名前})
static std::string Drain(){
    std::string s;
    Event ev;
    while(KeyInputFunctions::Pop(ev)){
        if(ev.key == Key::Char && !ev.ctrl()){ s += (char)ev.cp; continue; }
        if(ev.key == Key::Char && ev.ctrl()){ s += "{^"; s += (char)ev.cp; s += "}"; continue; }
        const char* n = "?";
        switch(ev.key){
            case Key::Enter: n = "Enter"; break;      case Key::Backspace: n = "BS"; break;
            case Key::Tab: n = "Tab"; break;          case Key::Escape: n = "Esc"; break;
            case Key::Delete: n = "Del"; break;       case Key::Left: n = "Left"; break;
            case Key::Right: n = "Right"; break;      case Key::Up: n = "Up"; break;
            case Key::Down: n = "Down"; break;        default: break;
        }
        s += "{"; if(ev.shift()) s += "S-"; s += n; s += "}";
    }
    return s;
}

// キー番号
enum : uint8_t { K1 = 0, K2 = 1, KQ = 11, KE = 13, KI = 18, KDEL = 21, KAA = 22, KA = 23, KS = 24, KD = 25,
                 KH = 28, KENTER = 32, KFN = 33, KSYM = 34, KZ = 35, KX = 36, KC = 37, KV = 38, KSPACE = 42 };

int main(){
    KeyInputFunctions::Setup();

    // ================= フレーム =================
    CardKbUart::Reset(); ClearQueue();
    Tap(KH); Tap(KI);
    check(Drain() == "hi" && CardKbUart::FramesReceived() == 4, "フレーム: 押した/離したで2文字");

    {
        std::string s; Frame(KA, true, &s);
        s[4] = (char)(s[4] + 1);    //チェックサム違い
        FeedStr(s); CardKbUart::Tick(now);
        check(Drain() == "" && CardKbUart::BytesDropped() > 0, "フレーム: チェックサム違いは捨てる");
        Release(KA); ClearQueue();
    }
    {
        //途中から受け取った(後ろ3バイト)+ゴミ+AAの入ったゴミ+正しいフレーム
        std::string s; Frame(KQ, true, &s);
        std::string junk = s.substr(2) + std::string("\x12\x34", 2) + std::string("\xAA\x03\x99", 3);
        std::string good; Frame(KE, true, &good); Frame(KE, false, &good);
        FeedStr(junk + good); CardKbUart::Tick(now);
        check(Drain() == "e", "フレーム: 途中から/ゴミ混じりでも次のAAから読み直す");
    }
    {
        //1バイトずつ、間にTick
        std::string s; Frame(KS, true, &s); Frame(KS, false, &s);
        for(unsigned char c : s){ CardKbUart::Feed(c, now); CardKbUart::Tick(now); }
        check(Drain() == "s", "フレーム: 1バイトずつ届いても同じ");
    }
    {
        std::string s; Frame(10, true, &s); Frame(10, false, &s); Frame(60, true, &s);
        FeedStr(s); CardKbUart::Tick(now);
        check(Drain() == "", "フレーム: 欠番・範囲外のキー番号は打鍵にしない");
    }

    // ================= 文字・修飾キー =================
    CardKbUart::Reset(); ClearQueue();
    Tap(K1); Tap(KDEL); Tap(KENTER); Tap(KSPACE);
    check(Drain() == "1{BS}{Enter} ", "文字: 数字・Delete(後退)・Enter・空白");

    //Aaを押している間は大文字
    Press(KAA); Tap(KA); Tap(KS); Release(KAA); Advance(300); Tap(KA);
    check(Drain() == "ASa", "Aa: 押している間だけ大文字");

    //1回押しは次の英字1文字だけ(離してすぐ打っても効く)
    Tap(KAA); Tap(KH); Tap(KI);
    check(Drain() == "Hi", "Aa: 1回押しは次の英字1文字だけ(すぐ打っても効く)");
    Tap(KAA); Tap(K1); Tap(KH); Tap(KI);
    check(Drain() == "1Hi", "Aa: 数字では使い切らない");
    Advance(300);

    //2回押しで固定、もう1回で解除
    Tap(KAA); Tap(KAA); Advance(300);
    Tap(KA); Tap(KS); Tap(KD);
    check(Drain() == "ASD", "Aa: 2回押しで固定");
    Tap(KAA); Advance(300); Tap(KA);
    check(Drain() == "a", "Aa: 固定中の1回押しで解除");

    //Aaを長押しして離しても1回押しにならない
    Press(KAA); Advance(500); Release(KAA); Advance(300); Tap(KA);
    check(Drain() == "a", "Aa: 長押しは1回押しにならない");

    //Sym
    Press(KSYM); Advance(500); Tap(K1); Tap(KQ); Release(KSYM); Advance(300); Tap(KQ);
    check(Drain() == "!~q", "Sym: 押している間だけ記号");
    Tap(KSYM); Tap(K2); Tap(KV); Tap(KSYM); Tap(K2);
    check(Drain() == "@<2", "Sym: 1回押しで切り替え、もう1回で戻す");
    Advance(300);

    // ================= Fn =================
    Press(KFN);
    Tap(KD); Tap(KZ); Tap(KX); Tap(KC); Tap(K1); Tap(KDEL); Tap(KSPACE); Tap(KENTER);
    check(Drain() == "{Up}{Left}{Down}{Right}{Esc}{Del}{Tab}{Enter}", "Fn: 矢印・Esc・Delete・Tab・Enter");
    Tap(KS); Tap(KV); Tap(KA); Tap(K2);
    check(Drain() == "{^s}{^v}{^a}", "Fn: 英字はCtrl+英字(小文字)、数字は割り当てなし");
    Release(KFN);
    Tap(KD);
    check(Drain() == "d", "Fn: 離せば元の文字");
    Press(KAA); Press(KFN); Tap(KZ); Release(KFN); Release(KAA);
    check(Drain() == "{S-Left}", "Fn: Aaを押していれば矢印にShift");
    Advance(300);

    // ================= 繰り返し =================
    ClearQueue();
    Press(KA);
    check(Drain() == "a", "繰り返し: 押した瞬間に1回");
    Advance(290);
    check(Drain() == "", "繰り返し: 300msまでは繰り返さない");
    Advance(100);   //t=390: 300と350で2回
    check(Drain() == "aa", "繰り返し: 300ms後から50msごと");
    Release(KA); Advance(200);
    check(Drain() == "", "繰り返し: 離せば止まる");

    Press(KFN); Press(KX); Advance(390); Release(KX); Release(KFN);
    check(Drain() == "{Down}{Down}{Down}", "繰り返し: Fn+Xの矢印も繰り返す");

    Press(KSYM); Advance(1000); Release(KSYM);
    check(Drain() == "", "繰り返し: 修飾キーは繰り返さない");

    Press(KH); Advance(CardKbUart::kMaxRepeatMs + 500); ClearQueue(); Advance(500);
    check(Drain() == "", "繰り返し: 離したフレームが来なくても長すぎれば止まる");
    Release(KH);

    // ================= 設定と配線 =================
    //cardkb.cfg が無ければI2C(呼びかけをする)
    OSData::SD_usable = true;
    HostSd::files.clear();
    PicoPcWire::present_addr = CardKbFunctions::kAddress;
    CardKbFunctions::Setup();
    CardKbFunctions::UpdateAt(0);
    check(CardKbFunctions::GetMode() == CardKbFunctions::Mode::I2C && CardKbFunctions::IsConnected(),
          "設定: cardkb.cfg が無ければI2Cで読む");

    //mode = uart
    HostSd::files[PICO_Path::FILE::CFG::SYS_CARDKB_CFG] = "mode = uart\n";
    ClearQueue();
    CardKbFunctions::Setup();
    check(CardKbFunctions::GetMode() == CardKbFunctions::Mode::Uart, "設定: mode = uart");
    CardKbFunctions::UpdateAt(10);
    check(!CardKbFunctions::IsConnected(), "UART: 何も届かなければつながっていない扱い(I2Cは見ない)");
    {
        std::string s;
        Frame(KH, true, &s); Frame(KH, false, &s); Frame(KI, true, &s); Frame(KI, false, &s);
        for(unsigned char c : s) PicoPcSerialPio::rx.push_back(c);
    }
    CardKbFunctions::UpdateAt(20);
    check(CardKbFunctions::IsConnected() && Drain() == "hi" && PicoPcSerialPio::rx.empty(),
          "UART: SerialPIOのバイトが打鍵になる");
    for(int i = 0; i < 200; i++) PicoPcSerialPio::rx.push_back(0x55);
    CardKbFunctions::UpdateAt(30);
    check(PicoPcSerialPio::rx.size() == 200 - CardKbFunctions::kUartMaxBytesPerUpdate, "UART: 1回に読む量に上限");
    PicoPcSerialPio::rx.clear();

    //PIOの空きが無い
    PicoPcSerialPio::begin_ok = false;
    log_warn = 0;
    CardKbFunctions::Setup();
    check(log_warn == 1, "UART: 始められなければ警告を1回");
    PicoPcSerialPio::rx.push_back(0xAA);
    CardKbFunctions::UpdateAt(40);
    check(PicoPcSerialPio::rx.size() == 1, "UART: 始められなければ読まない");
    PicoPcSerialPio::begin_ok = true;
    PicoPcSerialPio::rx.clear();

    //読めない値はI2Cのまま
    HostSd::files[PICO_Path::FILE::CFG::SYS_CARDKB_CFG] = "mode = usb\n";
    log_warn = 0;
    CardKbFunctions::Setup();
    check(CardKbFunctions::GetMode() == CardKbFunctions::Mode::I2C && log_warn == 1, "設定: 読めない値は警告してI2C");

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
