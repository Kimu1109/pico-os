#pragma once
#include <cstdint>

// Wiiクラシックコントローラー(SUMMARY.md #10 の実物の入力元)。
//
// 読んだボタンは PadFunctions::SetDevice() へ渡すだけで、アプリから見えるのは今までどおり
// PadFunctions(USBシリアルの代用入力とORで重なる)。無いのが普通なので、つながっていなくても何も出さない。
//
// ---- 配線 ----
// I2C0(SDA=GP0, SCL=GP1。hal/I2C_Bus.hpp、CardKB2と共有)。3.3Vで動かす(Wiiリモコンの拡張端子も3.3V)。
// 本体のプルアップはコントローラー側に無いので、SDA/SCLを3.3Vへ4.7kΩ程度でプルアップすること
// (CardKB2をつないでいればそちらのプルアップで足りる)。変換基板(Nunchuckyなど)を使うと楽。
//
// ---- プロトコル(アドレス 0x52、暗号化しない初期化) ----
//   接続を見つけたら: [0xF0, 0x55] → [0xFB, 0x00] を書く(純正もクローン品もこれで暗号化が外れる)
//   読み出し: [0x00] を書いて変換を始め、次の回(kPollIntervalMs 後)に6バイト読み、すぐ次の[0x00]を書く
//             (書いてから読むまでに待ちが要る機器があるので、待つ代わりに1回ずらす。止まらない)
//   6バイト(標準の形式。ボタンは0=押している):
//     [0] RX<4:3> LX<5:0>   [1] RX<2:1> LY<5:0>   [2] RX<0> LT<4:3> RY<4:0>   [3] LT<2:0> RT<4:0>
//     [4] BDR BDD BLT B- BH B+ BRT 1               [5] BZL BB BY BA BX BZR BDL BDU
// 左スティックは十字キーとしても読む(つながった直後の値を中心とし、kStickThreshold 以上傾けたら押した扱い)。
// クラシックコントローラーPro・ファミコン/スーファミのミニの付属コントローラー(同じ形式)もそのまま使える。
//
// ---- 接続の検出 ----
// 未接続の間は kProbeIntervalMs ごとに初期化を試み、ACKがあれば接続。接続中に読み出しが kMissLimit 回
// 続けて失敗する(NACK・6バイト来ない・全部0xFF=初期化されていない)と外れた扱いにして全部離す。
namespace WiiClassicFunctions {

    constexpr uint8_t       kAddress         = 0x52;
    constexpr unsigned long kProbeIntervalMs = 500;
    constexpr unsigned long kPollIntervalMs  = 10;
    constexpr int           kMissLimit       = 3;
    constexpr int           kStickThreshold  = 12;     // 左スティック(0〜63)の中心からの傾き

    void Setup();
    // loop()の先頭で毎フレーム。PadFunctions::Update() より前に呼ぶ(同じフレームに反映するため)
    void Update();
    // テスト用: 時刻を外から与える版
    void UpdateAt(unsigned long now_ms);

    bool IsConnected();
    uint16_t Buttons();     // PadFunctions::Button のOR

    // 6バイト → ボタン(PadFunctions::Button のOR)。読めない値(全部0xFF・ボタンが全部押されている)はfalse。
    // center_x/center_y は左スティックの中心(負なら十字キーとして読まない)
    bool Decode(const uint8_t data[6], int center_x, int center_y, uint16_t& buttons);
}
