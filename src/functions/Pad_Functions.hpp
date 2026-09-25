#pragma once
#include <cstdint>
#include <cstddef>

// 外部コントローラー(SUMMARY.md #10)の入力の窓口。
//
// アプリ(GameBoyScene / Lua)が見るのは「今押しているボタンのビットマスク」だけで、
// それがどこから来たか(USBシリアル / 将来のWiiクラシックコントローラー等)は知らない。
// 状態は1コア目の loop() の先頭(PICO_Touch::Update()の直後)で1回だけ更新するので、
// 1フレームの間は IsDown() / Pressed() / Released() の答えが変わらない。
//
// ---- 今の入力元: USBシリアル(PCのキーボード) ----
// 実物のコントローラーがまだ無いので、PC側の script/pad_serial.py がキーボードの状態を
// USBシリアル(ログを出しているのと同じ Serial)で送ってくる。1行1件のテキスト:
//
//     pad XXXX\n       XXXX = 押しているボタン(Button のOR)の16進数(1〜4桁、大文字小文字どちらでも)
//
// - PC側は**状態が変わったときと、変わらなくても100msごとに**送る(送り直すのは次の理由)
// - **kSerialTimeoutMs の間1行も届かなければ「外れた」扱いにして全部離す**。スクリプトを止めた・
//   ケーブルを抜いた・キーを離した瞬間の行が落ちた、のどれでもボタンが押しっぱなしにならない
// - 形式に合わない行は黙って捨てる(ログの逆向きなので、シリアルモニタから手で打っても害は無い。
//   逆に言えば、シリアルモニタで `pad 10` と打てばAを押したことになる)
// - 1回の Update() で読むのは kMaxBytesPerUpdate バイトまで(送り付けられてもフレームが止まらない)
//
// PCビルドでは pc/compat/Arduino.h の Serial が標準入力を読むので、
// `python3 script/pad_serial.py --stdout | ./pc/build/picoos_pc` で同じ経路を試せる。
namespace PadFunctions {

    // ボタンのビット。Wiiクラシックコントローラーにあるボタンを一通り持っておく。
    // **値はシリアルの取り決め(pad_serial.py)とLuaの名前表に直結しているので、並びを変えないこと**
    enum Button : uint16_t {
        Up     = 1u << 0,
        Down   = 1u << 1,
        Left   = 1u << 2,
        Right  = 1u << 3,
        A      = 1u << 4,
        B      = 1u << 5,
        X      = 1u << 6,
        Y      = 1u << 7,
        L      = 1u << 8,
        R      = 1u << 9,
        ZL     = 1u << 10,
        ZR     = 1u << 11,
        Start  = 1u << 12,
        Select = 1u << 13,
        Home   = 1u << 14,
    };
    constexpr uint16_t kAllButtons = 0x7FFF;
    constexpr int      kButtonCount = 15;

    enum class Source : uint8_t {
        None,       // 何もつながっていない
        Serial,     // USBシリアル(PCのキーボード)
    };

    constexpr unsigned long kSerialTimeoutMs   = 500;
    constexpr size_t        kMaxBytesPerUpdate = 256;
    constexpr size_t        kLineMax           = 32;

    void Setup();
    // Serialから届いた分を読んで状態を更新する(loop()の先頭で毎フレーム)
    void Update();
    // テスト用: 時刻を外から与える版(Update()は millis() を渡すだけ)
    void UpdateAt(unsigned long now_ms);

    bool IsConnected();
    Source GetSource();

    uint16_t Buttons();                 // 今押しているもの
    bool IsDown(uint16_t buttons);      // どれか1つでも押していればtrue
    bool Pressed(uint16_t buttons);     // このフレームで押されたものがあればtrue
    bool Released(uint16_t buttons);    // このフレームで離されたものがあればtrue

    // "up" "a" "start" 等(小文字) → ビット。知らない名前は0
    uint16_t ButtonFromName(const char* name);
    const char* ButtonName(int bit_index);  // 0〜kButtonCount-1。範囲外はnullptr

    // 1行("pad XXXX"、改行は含まない)を読む。読めればoutへ入れてtrue
    bool ParseLine(const char* line, uint16_t& out);
}
