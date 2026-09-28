#include "functions/Battery_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <Arduino.h>
#include <cstdlib>

// 実機とPCで読み取り方法が全く異なる(実機はADC+CYW43ロック、PCは環境変数)ため、
// LGFX_Config.hpp/Touch_Functions.hppと同じ「#if defined(PICOOS_PC)で分岐する」
// 数少ない例外としてこのファイル自身に閉じ込める(専用のpc/compat差し替えヘッダは
// 作らず、この1ファイル内で完結させる)
#if !defined(PICOOS_PC)
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"

// arduino-pico(rpipico2wボード定義)には、標準pico-sdkのボードヘッダが本来定義するはずの
// PICO_VSYS_PIN/CYW43_USES_VSYS_PINが無い(実機ビルドで`'PICO_VSYS_PIN' was not declared`を
// 実際に踏んで判明した)。このプロジェクトはPico 2 W(CYW43439搭載)専用で、無線チップと
// ADC3=GP29がSPI CLKピンを共用しているという配線そのものは変わらないため、
// マクロの有無に頼らず値を直接定義する(常にCYW43のロックを取る。下のSampleNow()参照)
#ifndef PICO_VSYS_PIN
#define PICO_VSYS_PIN 29
#endif
#endif

namespace {
    bool          has_sample            = false;
    int           last_percent          = 0;
    float         last_voltage          = 0.0f;
    bool          last_externally_powered = false;
    unsigned long last_sample_ms        = 0;

    int VoltageToPercent(float voltage){
        float pct = (voltage - BatteryFunctions::kMinVoltage)
                  / (BatteryFunctions::kMaxVoltage - BatteryFunctions::kMinVoltage) * 100.0f;
        if(pct < 0.0f)   pct = 0.0f;
        if(pct > 100.0f) pct = 100.0f;
        return (int)(pct + 0.5f);
    }

#if defined(PICOOS_PC)
    // 実機のVSYS/CYW43は無いので、環境変数で疑似的な値を返す
    //   PICOOS_BATTERY_PERCENT=45 ./pc/build/picoos_pc
    // 未指定なら80(満タン寄り)固定。範囲外や数値でない値は既定値のまま無視する
    void SampleNow(){
        int percent = 80;
        if(const char* v = getenv("PICOOS_BATTERY_PERCENT")){
            char* end = nullptr;
            long parsed = strtol(v, &end, 10);
            if(end != v && parsed >= 0 && parsed <= 100) percent = (int)parsed;
        }
        last_percent = percent;
        last_voltage = BatteryFunctions::kMinVoltage
            + (BatteryFunctions::kMaxVoltage - BatteryFunctions::kMinVoltage) * (percent / 100.0f);
        // PCでは「充電中」を再現する手段が無いので、満タン(100)のときだけ給電中とみなす
        last_externally_powered = (percent >= 100);
        has_sample = true;
    }
#else
    void SampleNow(){
        // 無線チップのSPIバスへの排他ロック。この区間中は無線側のバックグラウンド処理
        // (cyw43_arch_poll等)が止まるので、ADCの読み取りとSPI通信が同時に走らない。
        // Pico 2 W専用のこのプロジェクトでは常に無線チップがある前提で無条件に取る
        cyw43_thread_enter();
        // 無線チップを起こす(VBUS検出のついでに「給電中か」も分かる。公式power_status.cと同じ手順)。
        // WL_GPIO2がVBUS検出ピン(HIGH=VBUS有り)。マクロ名CYW43_WL_GPIO_VBUS_PINは
        // このボード定義(arduino-picoのrpipico2w)には無かった(実機ビルドで実際に踏んだ)ので、
        // ハードウェア上固定のインデックス2を直接渡す
        last_externally_powered = cyw43_arch_gpio_get(2);

        // ADC0=GP26が基準(SDK公式pico-examples power_status.cのPICO_FIRST_ADC_PINと同じ)
        constexpr int kFirstAdcPin  = 26;
        constexpr int kSampleCount  = 3;

        adc_gpio_init(PICO_VSYS_PIN);
        adc_select_input(PICO_VSYS_PIN - kFirstAdcPin);
        adc_fifo_setup(true, false, 0, false, false);
        adc_run(true);

        // 読み始めは低い値が混じるので捨てる(公式実装と同じ対策)
        int ignore_count = kSampleCount;
        while(!adc_fifo_is_empty() || ignore_count-- > 0){
            (void)adc_fifo_get_blocking();
        }

        uint32_t sum = 0;
        for(int i = 0; i < kSampleCount; i++){
            sum += adc_fifo_get_blocking();
        }

        adc_run(false);
        adc_fifo_drain();

        cyw43_thread_exit();

        // VSYSは分圧(1/3)された値がADCへ来るので3倍して戻す
        constexpr float kConversionFactor = 3.3f / 4096.0f;
        last_voltage = (sum / (float)kSampleCount) * 3.0f * kConversionFactor;
        last_percent = VoltageToPercent(last_voltage);
        has_sample   = true;
    }
#endif
}

void BatteryFunctions::Setup(){
#if !defined(PICOOS_PC)
    adc_init();
#endif
    last_sample_ms = millis();
    SampleNow();

    LOG_SYS_OK("Battery Setup has succeeded! (%d%%, %.2fV)", last_percent, last_voltage);
}

void BatteryFunctions::Update(){
    const unsigned long now = millis();
    if(now - last_sample_ms < kSampleIntervalMs) return;

    last_sample_ms = now;
    SampleNow();
}

bool BatteryFunctions::HasSample(){
    return has_sample;
}

int BatteryFunctions::GetPercent(){
    return last_percent;
}

float BatteryFunctions::GetVoltage(){
    return last_voltage;
}

bool BatteryFunctions::IsExternallyPowered(){
    return last_externally_powered;
}
