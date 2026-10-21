#pragma once
#include <cstdint>
#include <cstddef>
#include "system/KeyInput_Functions.hpp"

// CardKB2(M5Stack Unit CardKB2。42キーのQWERTYをI2Cで読む小型キーボード)の入力元。
//
// **あれば使える程度の扱い**(外部コントローラーと同じ)。無いのが普通なので、
// 接続されていなくてもログに失敗は出さず、画面にも何も出さない。
// 打鍵は KeyInputFunctions の列へ積むだけで、届け先(画面→キー盤)は USBシリアルのキーボードと共通。
//
// ---- 配線 ----
// I2C0: SDA=GP0, SCL=GP1(hal/I2C_Bus.hpp。Wiiクラシックコントローラーと同じバスで、アドレスは別)。
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
// ---- UARTモード(/sys/cardkb.cfg に "mode = uart" と書いたときだけ) ----
// CardKB2をFn+Sym+2でUARTモードにし、CardKB2のTX(Groveの白)を GP7(CARDKB_UART_RX)へ。
// 115200bpsをSerialPIOで受け、押した/離したのフレームを CardKbUart(hal/CardKB_Uart.hpp)が打鍵へ直す。
// I2Cモードと違い Fn+D/Z/X/C の矢印・Fn+1のEsc・Fn+英字のCtrlが使える。このときI2Cの呼びかけはしない
// (同じバスのWiiクラシックは自分でI2Cを始める)。設定は起動時に読むだけ(変えたら再起動)。
//
// ---- 接続の検出 ----
// 未接続の間は kProbeIntervalMs ごとにアドレスへ呼びかけ、ACKがあれば接続。
// 接続中は kPollIntervalMs ごとに1バイト読み、読み出し自体が kMissLimit 回続けて失敗したら外れた扱い。
//
// Wireのピン割り当てと begin() は I2cBus::Begin()(Wiiクラシックのドライバと共通)。
namespace CardKbFunctions {

    constexpr uint8_t       kAddress         = 0x5F;
    constexpr unsigned long kProbeIntervalMs = 1000;
    constexpr unsigned long kPollIntervalMs  = 10;
    constexpr int           kMissLimit       = 3;
    constexpr int           kMaxReadsPerUpdate = 4;

    constexpr unsigned long kUartBaud              = 115200;
    constexpr size_t        kUartFifoBytes         = 64;    // 12フレーム。毎フレーム読むので十分
    constexpr int           kUartMaxBytesPerUpdate = 128;

    enum class Mode : uint8_t { I2C, Uart };

    void Setup();
    // loop()の先頭付近で毎フレーム(PadFunctions::Update()の後)
    void Update();
    // テスト用: 時刻を外から与える版
    void UpdateAt(unsigned long now_ms);

    // I2C: 今つながっているか / UART: 正しいフレームを1つでも受け取ったか(抜けたことは分からない)
    bool IsConnected();
    Mode GetMode();

    // UARTで受信できているか(mode = uart で受信を始められた)。押している間が分かるのはこのときだけ
    bool UartActive();
    // UARTのとき今押しているキー(CardKbUartの番号のビット)。それ以外は0
    uint64_t HeldKeys();
    // trueの間はCardKB2の打鍵を文字として配らない(ゲームのボタンとして HeldKeys() で読む画面向け)。UARTのときだけ効く
    void SetGameMode(bool on);

    // 1バイト → 打鍵。打鍵として扱えない値(0=押していない、範囲外)はfalse
    bool Decode(uint8_t raw, KeyInputFunctions::Event& out);
}
