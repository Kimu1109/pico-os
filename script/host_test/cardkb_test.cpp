// CardKB2の入力元(CardKbFunctions)の検証。
//
// I2Cの先のキーボードは stubs/Wire.h(= pc/compat/Wire.h)の PicoPcWire で表し、時刻は UpdateAt() へ直接渡す。
// 確かめること:
//   - 変換(ASCII・制御キー・初代CardKBのカーソル・0やゴミは捨てる)
//   - 何もつながっていなくても静かで、打鍵も来ない / 呼びかけは kProbeIntervalMs ごと
//   - 後から刺すと検出され、押したキーが列へ積まれる
//   - 押していない(0)は積まない / 1回の更新で読む量に上限がある
//   - 抜くと kMissLimit 回の失敗で外れ、また刺せば戻る
#include "functions/CardKB_Functions.hpp"
#include "functions/KeyInput_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <Wire.h>
#include <cstdio>

static int log_ok = 0, log_msg = 0, log_other = 0;
void LogFunctions::Log(LogFunctions::LogType t, const char*, ...){
    if(t == LogFunctions::LogType::SYS_OK) log_ok++;
    else if(t == LogFunctions::LogType::SYS_MSG) log_msg++;
    else log_other++;
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace CardKbFunctions;
using KeyInputFunctions::Key;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    // ---- 変換 ----
    KeyInputFunctions::Event ev;
    check(Decode('a', ev) && ev.key == Key::Char && ev.cp == 'a' && ev.isPlainChar(), "decode: 'a'");
    check(Decode('A', ev) && ev.cp == 'A', "decode: 'A'(Shift反映済み)");
    check(Decode(' ', ev) && ev.cp == ' ', "decode: space");
    check(Decode(0x7E, ev) && ev.cp == '~', "decode: ~");
    check(Decode(0x08, ev) && ev.key == Key::Backspace, "decode: BS");
    check(Decode(0x7F, ev) && ev.key == Key::Backspace, "decode: DEL");
    check(Decode(0x0D, ev) && ev.key == Key::Enter, "decode: CR");
    check(Decode(0x0A, ev) && ev.key == Key::Enter, "decode: LF");
    check(Decode(0x09, ev) && ev.key == Key::Tab, "decode: Tab");
    check(Decode(0x1B, ev) && ev.key == Key::Escape, "decode: Esc");
    check(Decode(0xB4, ev) && ev.key == Key::Left && Decode(0xB5, ev) && ev.key == Key::Up &&
          Decode(0xB6, ev) && ev.key == Key::Down && Decode(0xB7, ev) && ev.key == Key::Right, "decode: 初代CardKBのカーソル");
    check(!Decode(0x00, ev), "decode: 0(押していない)は捨てる");
    check(!Decode(0x01, ev) && !Decode(0x1C, ev) && !Decode(0x80, ev) && !Decode(0xFF, ev), "decode: ゴミは捨てる");

    // ---- 未接続 ----
    Setup();
    PicoPcWire::present_addr = -1;
    UpdateAt(0);
    UpdateAt(500);
    UpdateAt(1500);
    check(!IsConnected(), "未接続: つながっていない");
    check(KeyInputFunctions::Pending() == 0, "未接続: 打鍵は来ない");
    check(log_ok == 0 && log_msg == 0 && log_other == 0, "未接続: ログに何も出さない");

    // ---- 後から刺す ----
    PicoPcWire::present_addr = kAddress;
    UpdateAt(1600);     // 前回の呼びかけから1秒未満
    check(!IsConnected(), "刺した直後でも呼びかけの間隔までは検出しない");
    UpdateAt(2500);
    check(IsConnected() && log_ok == 1, "接続を検出してログを1回出す");

    // ---- 打鍵 ----
    UpdateAt(2510);
    check(KeyInputFunctions::Pending() == 0, "押していなければ積まない");
    PicoPcWire::keys.push_back('h');
    UpdateAt(2515);     // 前回の読み出しから10ms未満
    check(KeyInputFunctions::Pending() == 0, "読み出しの間隔を待つ");
    UpdateAt(2530);
    check(KeyInputFunctions::Pending() == 1, "押したキーが列へ積まれる");
    check(KeyInputFunctions::Pop(ev) && ev.key == Key::Char && ev.cp == 'h', "積まれたのは 'h'");

    for(char c : {'a','b','c','d','e','f'}) PicoPcWire::keys.push_back((uint8_t)c);
    PicoPcWire::keys.push_back(0x0D);
    UpdateAt(2600);
    check(KeyInputFunctions::Pending() == (size_t)kMaxReadsPerUpdate, "1回の更新で読む量に上限がある");
    UpdateAt(2620);
    UpdateAt(2640);
    check(KeyInputFunctions::Pending() == 7, "残りは次の更新で読む");
    KeyInputFunctions::DiscardPending();

    PicoPcWire::keys.push_back(0x01);
    PicoPcWire::keys.push_back('z');
    UpdateAt(2700);
    check(KeyInputFunctions::Pending() == 1, "ゴミは積まず、続きは読む");
    KeyInputFunctions::DiscardPending();

    // ---- 抜く ----
    PicoPcWire::present_addr = -1;
    UpdateAt(3000);
    UpdateAt(3020);
    check(IsConnected(), "1〜2回の失敗では外れない");
    UpdateAt(3040);
    check(!IsConnected() && log_msg == 1, "kMissLimit回続けて失敗すると外れる");

    // ---- また刺す ----
    PicoPcWire::present_addr = kAddress;
    UpdateAt(3100);
    check(!IsConnected(), "外れた直後は呼びかけの間隔を待つ");
    UpdateAt(4100);
    check(IsConnected() && log_ok == 2, "刺し直すと戻る");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
