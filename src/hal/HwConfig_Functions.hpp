#pragma once

#include <cstddef>
#include "config/HwConfig.hpp"
#include "system/HwConfig_Codec.hpp"

// 液晶・タッチ・SDの配線と設定を内蔵フラッシュの末尾4KB(storage/Eeprom_Area)から読み、起動時の初期化へ渡す。
// 書き換えはUSBシリアルの "hwcfg ..." だけ(画面が映らないときでも直せるように)。反映は再起動から。
//
// 起動の流れ(main.cpp):
//   Load() → PICO_GFX::Setup() / PICO_SD::Setup() / PICO_Touch::Setup() が active を使う。
//   保存した値で液晶が応答しない・SDが開けないときは、その部分(液晶+タッチ / SD)だけ既定へ戻して試し、
//   既定でうまくいけばそちらで起動する(CanFallBack() / WithDefault() / MarkFellBack())。
//   既定でもだめなら保存した値のまま続ける(読み出しの線の無い液晶・SDが入っていない、のときに既定へ
//   化けないように)。フラッシュの値は書き換えない。
// 詳しくは docs/design/devtools.md「液晶・SDの配線を内蔵フラッシュに置く」。
namespace HwConfigFunctions {
    enum class Source : unsigned char {
        Default, // 何も保存していない
        Stored,  // 保存した値で起動した
        Broken,  // 保存した値が壊れていた(CRC・形)。既定で起動した
        Invalid, // 保存した値が使えない組み合わせだった。既定で起動した
    };

    // 初期化に使う値(フォールバックしたらその部分は既定になっている)
    inline HwConfig active = HwConfigDefault();
    // 内蔵フラッシュにある値(Broken なら既定)
    inline HwConfig stored = HwConfigDefault();
    // シリアルで編集中の値("hwcfg save" で stored へ書く)
    inline HwConfig edit = HwConfigDefault();
    inline Source source = Source::Default;
    inline bool lcd_fell_back = false;
    inline bool sd_fell_back = false;

    // 再起動の差し込み口(boot/ が PowerMenuFunctions::RequestReboot を差す)
    inline void (*reboot)() = nullptr;

    // 内蔵フラッシュから読んで active / stored / edit を決める。液晶・SDの初期化より前に1回
    void Load();

    // 保存した値で起動していて、その部分が既定と違う(=既定へ戻して試す意味がある)
    bool CanFallBack(HwConfigCodec::Group g);
    // active のその部分だけ既定にしたもの
    HwConfig WithDefault(HwConfigCodec::Group g);
    // 既定へ戻した値で初期化できたので、それを active にする
    void MarkFellBack(HwConfigCodec::Group g, const HwConfig& used);

    // 検査して内蔵フラッシュへ書く。だめなら false と理由
    bool Save(const HwConfig& c, char* why, size_t cap);
    // 内蔵フラッシュの設定を消す(次の起動から既定)
    bool Erase();

    // USBシリアルの1行。"hwcfg" で始まる行なら扱って true
    bool FeedSerialLine(const char* line);

    // 既定へ戻して起動した・保存した値が読めなかったことを通知へ出す(NotificationFunctions::Setup() より後)
    void PostPendingNotice();
}
