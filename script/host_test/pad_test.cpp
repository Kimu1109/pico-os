// 外部コントローラーの窓口(PadFunctions)の検証。
//
// USBシリアルの代わりに stubs/Arduino.h の HostSerial::Feed() で行を流し込み、
// 時刻は UpdateAt() に直接渡す。確かめること:
//   - "pad XXXX" の読み取り(大文字小文字・桁数・前後の空白・壊れた行を捨てること)
//   - 押した/離したのはそのフレームだけ(Pressed/Released)
//   - 行が途切れたら kSerialTimeoutMs で外れた扱いになり、押しっぱなしにならない
//   - 1行が複数のUpdate()にまたがって届いてもよい / 長すぎる行は捨てる
//   - 1回のUpdate()で読む量に上限がある
//   - Game Boyのボタンへの対応(GbPadMap)
#include "functions/Pad_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gb/Gb_PadMap.hpp"

#include <Arduino.h>
#include <cstdio>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace PadFunctions;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static bool Parse(const char* s, uint16_t expect){
    uint16_t v = 0xFFFF;
    return ParseLine(s, v) && v == expect;
}
static bool Rejects(const char* s){
    uint16_t v = 0;
    return !ParseLine(s, v);
}

int main(){
    // ---- 行の読み取り ----
    check(Parse("pad 0", 0), "pad 0");
    check(Parse("pad 0010", A), "pad 0010 = A");
    check(Parse("pad 1f", Up | Down | Left | Right | A), "小文字の16進");
    check(Parse("pad 1F", Up | Down | Left | Right | A), "大文字の16進");
    check(Parse("pad   20  ", B), "前後の空白");
    check(Parse("pad ffff", kAllButtons), "使っていない上位ビットは落とす");
    check(Rejects("pad"), "値が無い");
    check(Rejects("pad "), "値が空");
    check(Rejects("pad 12345"), "5桁は捨てる");
    check(Rejects("pad 1g"), "16進でない文字");
    check(Rejects("pad 1 2"), "値の後ろにごみ");
    check(Rejects("PAD 1"), "頭は小文字のpadだけ");
    check(Rejects("[SYS] pad 1"), "ログらしき行は捨てる");

    // ---- 名前 ----
    check(ButtonFromName("up") == Up && ButtonFromName("home") == Home, "名前→ビット");
    check(ButtonFromName("UP") == 0 && ButtonFromName("jump") == 0, "知らない名前は0");
    bool names_ok = true;
    for(int i = 0; i < kButtonCount; i++){
        if(ButtonFromName(ButtonName(i)) != (1u << i)) names_ok = false;
    }
    check(names_ok && ButtonName(kButtonCount) == nullptr, "名前表がビット順と一致");

    // ---- 状態の流れ ----
    Setup();
    unsigned long t = 1000;
    UpdateAt(t);
    check(!IsConnected() && Buttons() == 0, "最初は何もつながっていない");

    HostSerial::Feed("pad 0010\n");
    UpdateAt(t += 16);
    check(IsConnected() && GetSource() == Source::Serial, "1行届けばつながった扱い");
    check(IsDown(A) && Pressed(A) && !Released(A), "Aを押した");

    UpdateAt(t += 16);
    check(IsDown(A) && !Pressed(A), "押した扱いはそのフレームだけ");

    HostSerial::Feed("pad 0011\r\n");  // CRLFでもよい
    UpdateAt(t += 16);
    check(IsDown(A) && IsDown(Up) && Pressed(Up) && !Pressed(A), "同時押し(十字キー+A)");

    HostSerial::Feed("pad 0001\n");
    UpdateAt(t += 16);
    check(Released(A) && !Released(Up) && IsDown(Up), "Aだけ離した");

    // 1行が2回に分かれて届く
    HostSerial::Feed("pad 00");
    UpdateAt(t += 16);
    check(IsDown(Up), "行の途中ではまだ変わらない");
    HostSerial::Feed("20\n");
    UpdateAt(t += 16);
    check(Buttons() == B, "続きが届いてから反映する");

    // 壊れた行は無視し、状態も時刻も進めない
    HostSerial::Feed("hello\n");
    UpdateAt(t += 16);
    check(Buttons() == B, "形式に合わない行は無視");

    // 長すぎる行は改行まで読み捨て、次の行はまた読める
    HostSerial::Feed("pad 1                                                  \npad 0040\n");
    UpdateAt(t += 16);
    check(Buttons() == X, "長すぎる行を捨てて次の行を読む");

    // ---- 途切れたら外れる ----
    UpdateAt(t += kSerialTimeoutMs - 50);
    check(IsConnected() && IsDown(X), "タイムアウトまでは押したまま");
    UpdateAt(t += 100);
    check(!IsConnected() && Buttons() == 0 && Released(X), "行が途切れたら外れて全部離す");

    HostSerial::Feed("pad 1000\n");
    UpdateAt(t += 16);
    check(IsConnected() && Pressed(Start), "また届けばつながり直す");

    // ---- 1回に読む量の上限 ----
    {
        std::string many;
        while(many.size() < kMaxBytesPerUpdate) many += "pad 0000\n";
        many += "pad 2000\n";
        HostSerial::Feed(many.c_str());
        UpdateAt(t += 16);
        check(!IsDown(Select), "上限を超えた分は次のフレームへ回す");
        UpdateAt(t += 16);
        check(IsDown(Select), "次のフレームで残りを読む");
    }

    // ---- Game Boyのボタン ----
    check(GbPadMap::ToGb(0) == 0, "GB: 何も押していない");
    check(GbPadMap::ToGb(Up | Right | A) == (GbEmu::Up | GbEmu::Right | GbEmu::A), "GB: 十字キー+A");
    check(GbPadMap::ToGb(Y) == GbEmu::B && GbPadMap::ToGb(B) == GbEmu::B, "GB: BとYはどちらもB");
    check(GbPadMap::ToGb(Start | Select) == (GbEmu::Start | GbEmu::Select), "GB: START/SELECT");
    check(GbPadMap::ToGb(Home | X | L | R) == 0, "GB: HOMEやXは渡さない");

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
