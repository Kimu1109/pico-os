#pragma once
#include <cstdint>

// 画面の明るさ調整と、一定時間操作が無かったときの自動調光(オートディム)。
//
// - 明るさは0〜100の百分率。実体は PICO_GFX::SetBrightness()(バックライトのPWM調光)を
//   呼ぶだけで、どの値を見せるかの方針だけをここが持つ。
// - kMinBrightness未満には設定できない(0まで許すと画面が真っ黒になり、
//   明るさを戻すためのスライダーすら見えなくなるため)。
// - 自動調光は「タッチ(OSData::isTouched)も外部コントローラー(PadFunctions)も
//   kIdleTimeoutMsの間ずっと無ければ暗くする」方式。SoundFunctionsの音量と同じく
//   即座に切り替える(フェードはしない。ClocksScene等と同じmillis()差分の考え方)。
// - 設定は /sys/display.cfg(無くてよい。既定値で動く):
//     brightness = 0〜100
//     auto-dim   = true | false
//   SettingsSceneが音量と同じ「操作の都度PICO_Config::SetValue()でその場に書く」流儀で書き込む。
namespace DisplayFunctions {

    // スライダー等から設定できる明るさの下限。これより下は真っ黒に近づき過ぎて
    // 操作不能になるため、SetBrightness()が下駄を履かせる
    constexpr uint8_t kMinBrightness = 10;
    constexpr uint8_t kDefaultBrightness = 100;

    // 自動調光で落とす先の明るさ(絶対値)。通常の明るさがこれより低ければそちらを使う。
    // 旧方式(パレット減光)で「15だと真っ黒にしか見えない」との指摘を受けて40にした値を
    // そのまま引き継いでいる。バックライトのPWMは光量そのものを絞るので、同じ数値でも
    // パレット減光より明るく見えるはず。実機で見て暗さが足りなければここを下げること
    constexpr uint8_t kDimBrightness = 40;
    // この時間操作が無ければ暗くする
    constexpr unsigned long kIdleTimeoutMs = 30000;

    void Setup();
    void Update();

    // 通常時(自動調光がかかっていないとき)の明るさ。display.cfgから読んだ値、
    // または直近にSetBrightness()した値
    uint8_t GetBrightness();
    // 通常の明るさを今だけ変える(display.cfgへは書かない。呼び出し側がSettingsSceneの
    // 音量と同じ流儀でPICO_Config::SetValue()すること)。自動調光で暗くなっている間に
    // 呼んでも、バックライトへは反映されない(次に明るさへ戻ったときの値として使われるだけ)
    void SetBrightness(int percent);

    bool GetAutoDimEnabled();
    // 今だけ変える(display.cfgへは書かない)
    void SetAutoDimEnabled(bool enabled);

    // 自動調光で今暗くなっているか
    bool IsDimmed();
}
