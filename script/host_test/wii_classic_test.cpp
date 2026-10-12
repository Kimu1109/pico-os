// Wiiクラシックコントローラーの入力元(WiiClassicFunctions)の検証。
//
// I2Cの先のコントローラーは stubs/Wire.h(= pc/compat/Wire.h)の PicoPcWire::devices に置く偽物で表し、
// 時刻は UpdateAt()/PadFunctions::UpdateAt() へ直接渡す。
// 確かめること:
//   - 6バイトの読み取り(ボタン15個の位置・0=押している・左スティックを十字キーとして・読めない値は捨てる)
//   - つながっていなければ静かで、呼びかけは kProbeIntervalMs ごと
//   - 見つけたら暗号化しない初期化([F0 55][FB 00])と変換の開始([00])を書く
//   - 押したボタンが PadFunctions に届き、USBシリアルの入力とORで重なる
//   - 抜くと kMissLimit 回の失敗で外れて全部離し、また刺せば戻る
#include "functions/WiiClassic_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <cstdio>
#include <cstring>
#include <vector>

void LogFunctions::Log(LogFunctions::LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace WiiClassicFunctions;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 偽物のコントローラー。書かれた列を覚え、初期化が済んでいれば今のボタン(1=押している)を6バイトで返す
struct FakeClassic : PicoPcWire::Device {
    bool present = true;
    bool inited = false;
    int  started = 0;                     // [00]を書かれた回数
    std::vector<std::vector<uint8_t>> writes;
    uint16_t pressed = 0;                 // PadFunctions::Button のOR
    int lx = 32, ly = 32;

    bool onWrite(const uint8_t* d, size_t n) override {
        if(!present) return false;
        writes.emplace_back(d, d + n);
        if(n == 2 && d[0] == 0xFB && d[1] == 0x00) inited = true;
        if(n == 1 && d[0] == 0x00) started++;
        return true;
    }
    size_t onRead(uint8_t* d, size_t n) override {
        if(!present) return 0;
        if(n < 6) return 0;
        if(!inited){ memset(d, 0xFF, 6); return 6; }
        using namespace PadFunctions;
        uint8_t b4 = 0x01, b5 = 0;   // b4のbit0は常に1
        if(pressed & Right)  b4 |= 0x80;
        if(pressed & Down)   b4 |= 0x40;
        if(pressed & L)      b4 |= 0x20;
        if(pressed & Select) b4 |= 0x10;
        if(pressed & Home)   b4 |= 0x08;
        if(pressed & Start)  b4 |= 0x04;
        if(pressed & R)      b4 |= 0x02;
        if(pressed & ZL)     b5 |= 0x80;
        if(pressed & B)      b5 |= 0x40;
        if(pressed & Y)      b5 |= 0x20;
        if(pressed & A)      b5 |= 0x10;
        if(pressed & X)      b5 |= 0x08;
        if(pressed & ZR)     b5 |= 0x04;
        if(pressed & Left)   b5 |= 0x02;
        if(pressed & Up)     b5 |= 0x01;
        d[0] = (uint8_t)(0x80 | (lx & 0x3F));
        d[1] = (uint8_t)(0x40 | (ly & 0x3F));
        d[2] = 0x10; d[3] = 0x00;
        d[4] = (uint8_t)(~(b4 & 0xFE) & 0xFF);   // 0=押している(bit0は1のまま)
        d[5] = (uint8_t)~b5;
        return 6;
    }
};

// Wii → Pad の順で1フレーム進める
static void Frame(unsigned long now){
    WiiClassicFunctions::UpdateAt(now);
    PadFunctions::UpdateAt(now);
}

int main(){
    using PadFunctions::Source;
    using PadFunctions::A; using PadFunctions::B; using PadFunctions::X; using PadFunctions::Right;

    // ---- 読み取り ----
    {
        FakeClassic f;
        f.inited = true;
        uint8_t d[6];
        uint16_t out = 0;
        using namespace PadFunctions;
        const uint16_t all[] = { Up, Down, Left, Right, A, B, X, Y, L, R, ZL, ZR, Start, Select, Home };
        bool each = true;
        for(uint16_t b : all){
            f.pressed = b;
            f.onRead(d, 6);
            if(!Decode(d, 32, 32, out) || out != b) each = false;
        }
        check(each, "decode: 15個のボタンがそれぞれの位置で読める");
        f.pressed = A | Left | Start;
        f.onRead(d, 6);
        check(Decode(d, 32, 32, out) && out == (A | Left | Start), "decode: 同時押し");
        f.pressed = 0; f.lx = 60; f.ly = 32;
        f.onRead(d, 6);
        check(Decode(d, 32, 32, out) && out == Right, "decode: 左スティックを右へ");
        f.lx = 32; f.ly = 4;
        f.onRead(d, 6);
        check(Decode(d, 32, 32, out) && out == Down, "decode: 左スティックを下へ(小さいほど下)");
        f.lx = 32 + kStickThreshold - 1; f.ly = 32;
        f.onRead(d, 6);
        check(Decode(d, 32, 32, out) && out == 0, "decode: 少し傾けただけでは押さない");
        f.lx = 60;
        f.onRead(d, 6);
        check(Decode(d, -1, -1, out) && out == 0, "decode: 中心が分からなければスティックは読まない");
        memset(d, 0xFF, 6);
        check(!Decode(d, 32, 32, out), "decode: 全部0xFF(初期化されていない)は捨てる");
        d[4] = 0x01; d[5] = 0x00;
        check(!Decode(d, 32, 32, out), "decode: 全部押している(読めていない)は捨てる");
    }

    // ---- 未接続 ----
    PadFunctions::Setup();
    WiiClassicFunctions::Setup();
    PicoPcWire::devices.clear();
    Frame(0);
    Frame(100);
    check(!WiiClassicFunctions::IsConnected() && !PadFunctions::IsConnected(), "つながっていなければ未接続のまま");

    // ---- 刺す ----
    FakeClassic pad;
    PicoPcWire::devices[kAddress] = &pad;
    Frame(kProbeIntervalMs - 1);
    check(!WiiClassicFunctions::IsConnected(), "呼びかけは kProbeIntervalMs ごと");
    Frame(kProbeIntervalMs);
    check(WiiClassicFunctions::IsConnected(), "刺すと見つかる");
    check(pad.writes.size() >= 3 && pad.writes[0] == std::vector<uint8_t>{0xF0, 0x55}
          && pad.writes[1] == std::vector<uint8_t>{0xFB, 0x00} && pad.writes[2] == std::vector<uint8_t>{0x00},
          "暗号化しない初期化 [F0 55][FB 00] と変換の開始 [00] を書く");
    check(PadFunctions::IsConnected() && PadFunctions::GetSource() == Source::WiiClassic, "PadFunctionsからつながって見える");

    unsigned long t = kProbeIntervalMs;
    pad.pressed = A;
    t += kPollIntervalMs; Frame(t);
    check(PadFunctions::IsDown(A) && PadFunctions::Pressed(A), "押したボタンが届く");
    const int starts = pad.started;
    t += kPollIntervalMs; Frame(t);
    check(pad.started == starts + 1, "読むたびに次の変換を始める");
    check(PadFunctions::IsDown(A) && !PadFunctions::Pressed(A), "押し続けると押したのは最初の1回");
    pad.pressed = 0;
    t += kPollIntervalMs; Frame(t);
    check(!PadFunctions::IsDown(A) && PadFunctions::Released(A), "離すと届く");

    // スティックの中心はつながった直後の値
    pad.lx = 50;
    t += kPollIntervalMs; Frame(t);
    check(PadFunctions::IsDown(Right), "左スティックで十字キー");
    pad.lx = 32;
    t += kPollIntervalMs; Frame(t);

    // USBシリアルとORで重なる
    pad.pressed = B;
    t += kPollIntervalMs;
    WiiClassicFunctions::UpdateAt(t);
    HostSerial::Feed("pad 0010\n");
    PadFunctions::UpdateAt(t);
    check(PadFunctions::IsDown(A) && PadFunctions::IsDown(B), "USBシリアル(A)とWii(B)が重なる");
    check(PadFunctions::GetSource() == Source::WiiClassic, "両方あるときはWiiと答える");

    // ---- 抜く ----
    pad.present = false;
    for(int i = 0; i < kMissLimit - 1; i++){ t += kPollIntervalMs; Frame(t); }
    check(WiiClassicFunctions::IsConnected(), "失敗が kMissLimit 回に届くまではつながったまま");
    t += kPollIntervalMs; Frame(t);
    check(!WiiClassicFunctions::IsConnected(), "kMissLimit 回失敗すると外れる");
    check(!PadFunctions::IsDown(B), "外れたらWiiのボタンは離した扱い");
    check(PadFunctions::IsConnected() && PadFunctions::GetSource() == Source::Serial, "USBシリアルは残る");

    // ---- 刺し直す ----
    pad.present = true;
    pad.inited = false;
    pad.writes.clear();
    pad.pressed = X;
    t += kProbeIntervalMs; Frame(t);
    check(WiiClassicFunctions::IsConnected() && pad.inited, "刺し直すと初期化し直して戻る");
    t += kPollIntervalMs; Frame(t);
    check(PadFunctions::IsDown(X), "刺し直した後のボタンも届く");

    PicoPcWire::devices.clear();
    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
