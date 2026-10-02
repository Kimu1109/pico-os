#pragma once
#include <cstdint>
#include "functions/KeyInput_Functions.hpp"

// CardKB2(M5Stack Unit CardKB2。42キーのQWERTYをI2Cで読む小型キーボード)の入力元。
//
// **あれば使える程度の扱い**(外部コントローラーと同じ)。無いのが普通なので、
// 接続されていなくてもログに失敗は出さず、画面にも何も出さない。
// 打鍵は KeyInputFunctions の列へ積むだけで、届け先(画面→キー盤)は USBシリアルのキーボードと共通。
//
// ---- 配線 ----
// I2C0: SDA=GP0, SCL=GP1(外部コントローラー用に取ってある端子。Wiiクラシックと同じバスで、アドレスは別)。
// 電源はGroveの5V。CardKB2側のI2Cのプルアップはキーボード側にあるので外付けは要らない(3.3V系のバスに
// 5Vのプルアップが乗らないか、実機で確かめること)。
//
// ---- プロトコル(M5Unit-KEYBOARDのUnitCardKB2より。I2Cモードの出荷時既定。Fn+Sym+1で切り替え、RSTで反映) ----
// - アドレス 0x5F(初代CardKBと同じ)。1バイト読むと、押されたキーのASCIIが1回だけ返る(押していなければ0)
// - 押した瞬間に返り、押し続けると300ms後から50msごとに繰り返す(リピートは本体側)
// - Shift(Aaキー)・Sym・Fnの結果は ASCII に反映済みで届く
// - **I2Cモードでは Fn+D/Z/X/C のカーソルキーは出ない**(本体の仕様)。初代CardKBが出す 0xB4〜0xB7 は
//   一応カーソルキーとして読むが、CardKB2では来ない
// - UARTモード(Fn+Sym+2)に切り替わっていると何も返らない(接続されていない扱いになる)
//
// ---- 接続の検出 ----
// 未接続の間は kProbeIntervalMs ごとにアドレスへ呼びかけ、ACKがあれば接続。
// 接続中は kPollIntervalMs ごとに1バイト読み、読み出し自体が kMissLimit 回続けて失敗したら外れた扱い。
//
// Wireのピン割り当てと begin() はここで行う。Wiiクラシックのドライバを足すときは、
// 同じバスなので begin() を共通の場所へ移すこと。
namespace CardKbFunctions {

    constexpr uint8_t       kAddress         = 0x5F;
    constexpr int           kSdaPin          = 0;
    constexpr int           kSclPin          = 1;
    constexpr uint32_t      kClockHz         = 100000;
    constexpr unsigned long kProbeIntervalMs = 1000;
    constexpr unsigned long kPollIntervalMs  = 10;
    constexpr int           kMissLimit       = 3;
    constexpr int           kMaxReadsPerUpdate = 4;

    void Setup();
    // loop()の先頭付近で毎フレーム(PadFunctions::Update()の後)
    void Update();
    // テスト用: 時刻を外から与える版
    void UpdateAt(unsigned long now_ms);

    bool IsConnected();

    // 1バイト → 打鍵。打鍵として扱えない値(0=押していない、範囲外)はfalse
    bool Decode(uint8_t raw, KeyInputFunctions::Event& out);
}
