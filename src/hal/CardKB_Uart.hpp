#pragma once
#include <cstdint>
#include "system/KeyInput_Functions.hpp"

// CardKB2のUARTモード(Fn+Sym+2)で届くバイト列を打鍵へ直す係。描画にもシリアルにも依存しない
// (受け取ったバイトを Feed() へ渡すのは CardKbFunctions)。
//
// ---- 届くもの(M5Unit-KEYBOARD の UnitCardKB2UART より。115200bps 8N1) ----
// 1回のキーの変化が5バイト: AA 03 <キー番号 0〜42> <01=押した / 02=離した> <(03+番号+状態)の下位8bit>
// キー番号は 行×11+列(4行×11列。10番は欠番)。**文字ではなく押した/離したしか来ない**ので、
// I2Cモードで本体がやっていた次のことをここでやる:
//   - キー番号 → 文字(通常 / Aa(Shift) / Sym の表)
//   - Aa: 押している間 / 1回押し=次の英字1文字だけ大文字 / 2回押し=固定(もう1回で解除)
//     Sym: 押している間 / 1回押し=切り替え(もう1回で戻す)。Fn: 押している間だけ
//     (本体のファームウェアと同じく、200ms以内の2回目を2回押しとみなし、400msより長く押したら「押している間」だけ。
//      ただし本体は離して200ms待ってから効かせるが、ここでは離したらすぐ効かせる。待つと続けて打った文字に間に合わない。
//      また、押している間に別のキーを打ったら1回押しに数えない=Shiftのように使える)
//   - 押しっぱなしの繰り返し(I2Cモードの本体と同じ 300ms後から50msごと)
//
// ---- Fn との組み合わせ(pico-os独自。I2Cモードには無い) ----
//   Fn+D/Z/X/C = ↑←↓→(CardKB2の印字どおり)  Fn+1 = Esc  Fn+Del = Delete  Fn+Space = Tab
//   Fn+それ以外の英字 = Ctrl+英字(Ctrl+S保存・Ctrl+V貼り付け等)。Ctrl+D/Z/X/C は矢印に取られて打てない
//   Aa(Shift)が効いている間は矢印にもShiftが付く(テキストエディタの範囲選択)
//
// 区切りは先頭のAAと長さ・状態・チェックサムで確かめ、合わなければ1バイトずらして探し直す
// (途中から受け取り始めた・ノイズが乗った場合)。
namespace CardKbUart {

    constexpr uint8_t       kHeader        = 0xAA;
    constexpr uint8_t       kLength        = 0x03;
    constexpr uint8_t       kStatePressed  = 0x01;
    constexpr uint8_t       kStateReleased = 0x02;
    constexpr int           kNumKeys       = 43;

    constexpr int kKeyDelete = 21;
    constexpr int kKeyAa     = 22;
    constexpr int kKeyEnter  = 32;
    constexpr int kKeyFn     = 33;
    constexpr int kKeySym    = 34;
    constexpr int kKeySpace  = 42;

    constexpr unsigned long kRepeatDelayMs  = 300;
    constexpr unsigned long kRepeatPeriodMs = 50;
    // 離したフレームが届かない(線が抜けた)ときに繰り返しが止まらなくならないよう、これより長く押したままなら止める
    constexpr unsigned long kMaxRepeatMs    = 10000;
    // 1回押し/2回押し/長押しの判定(本体のファームウェアの iot_button と同じ値)
    constexpr unsigned long kClickGapMs     = 200;
    constexpr unsigned long kLongPressMs    = 400;

    void Reset();
    // 受け取った1バイト。フレームがそろえば打鍵を KeyInputFunctions の列へ積む
    void Feed(uint8_t b, unsigned long now_ms);
    // 毎フレーム1回(Feed()の後)。1回押しの確定と繰り返し
    void Tick(unsigned long now_ms);

    // 正しく読めたフレームの数 / 捨てたバイトの数(診断・テスト用)
    uint32_t FramesReceived();
    uint32_t BytesDropped();

    // キー番号と修飾キーの状態 → 打鍵(テスト用に外へ出してある)。打鍵にならなければfalse
    bool KeyToEvent(int key, bool fn, bool sym, bool caps, KeyInputFunctions::Event& out);
}
