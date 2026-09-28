#pragma once

// VSYS電圧からLiPoバッテリーの残量を読み取る(PiCoLiPoSHIM等、VSYSへ電源を注入する
// 外付け充電器を想定)。
//
// - RP2350の無線チップ(CYW43439)とADC3(GP29)はSPIのCLK信号を共用しているため、
//   素のadc_read()では無線通信中の読み取りタイミングと衝突しうる
//   (衝突は「時々しか読まない」だけでは避けられない。タイマー割り込みで無線側の
//   処理が挟まる可能性があるため)。読み取りはcyw43_thread_enter()/cyw43_thread_exit()
//   で挟んで排他制御する(Raspberry Pi公式pico-examplesのpower_status.cと同じ手順)。
// - このロックは無線チップ側のバックグラウンド処理を一瞬止めるので、読み取り自体は
//   kSampleIntervalMsごとにしか行わない(ロックが必須なのは頻度に関わらずだが、
//   間隔を空けることでロックによる影響の回数を減らす)。
// - 電圧→百分率はLiPoの3.0V(空)〜4.2V(満充電)の単純な線形近似
//   (Raspberry Pi公式pico-examples read_vsys.cと同じ換算式。放電カーブの非線形性は無視)。
// - GFX_Functions/Touch_Functionsと同じ理由でホストテスト(ASan)の対象外。
//   検証はPCビルドの--shot(PICOOS_BATTERY_PERCENT環境変数で疑似値を固定できる)で行う。
//   実機(RP2350+CYW43439)でのVSYS読み取りは未確認
//   (このリモート環境にはRP2350の実機もボード定義も無いため)。
namespace BatteryFunctions {

    constexpr unsigned long kSampleIntervalMs = 60000;
    constexpr float kMinVoltage = 3.0f;  // 空(0%)とみなす電圧
    constexpr float kMaxVoltage = 4.2f;  // 満充電(100%)とみなす電圧

    void Setup();
    void Update();

    // 一度でも読み取れていればtrue(起動直後の1回はSetup()内で必ず読むので、
    // 正常なら常にtrueになる)
    bool HasSample();

    int   GetPercent();  // 0〜100
    float GetVoltage();

    // USB(VBUS)がPicoへ届いているか。PiCoLiPoSHIM等でVBUSが配線されていれば、
    // 充電中でも充電完了後でも(SHIM側の充電完了信号は配線していないため区別できない)真になる
    bool IsExternallyPowered();
}
