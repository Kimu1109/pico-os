#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

#include <cstdint>
#include <functional>

// Game Boyの操作パッド(十字キー / A / B / SELECT / START / ROM / 戻る)。GameBoyScene専用。
//
// 子を持たずrender()で直接描き、タップ位置からボタンを逆算する
// (AppGrid / TabBar / MonthGrid と同じ方式)。
//
//   ┌────────┬─────────┬─────────┐
//   │  十字  │ SELECT  │      (A)│
//   │  キー  │ START   │  (B)    │
//   │        │ ROM│戻る│         │
//   └────────┴─────────┴─────────┘
//
// ---- タッチは1点だけ ----
// XPT2046は同時に1点しか取れないので、「十字キーを押しながらA」はできない。
// 指を滑らせると押しているボタンが切り替わる(十字キーの中で指を回す、Aから十字キーへ移る等)。
// 十字キーは中心からの角度で8方向を取る(斜めは2方向を同時に押した扱い)。
//
// ROM / 戻る はゲームのボタンではなく、離した瞬間(その上で離したときだけ)に
// setOnRom() / setOnBack() のコールバックを呼ぶ。
class GameBoyPad : public Widget {
    public:
        constexpr static int kPadH = 84;

    private:
        enum class Area : uint8_t { None, Game, Rom, Back };

        uint8_t pressed = 0;          // GbEmu::Button のOR
        Area press_area = Area::None; // 押し始めた場所
        bool over_command = false;    // ROM/戻るの上に指が乗っているか(押している表示)

        std::function<void()> on_rom = nullptr;
        std::function<void()> on_back = nullptr;

        // ローカル座標(px,py)にあるものを返す。ゲームのボタンならbuttonsへ入れる
        Area areaAt(int px, int py, uint8_t& buttons) const;
        void track();
        void setPressed(uint8_t buttons, bool over);

        void drawDpad(int ox, int oy);
        void drawRoundButton(int cx, int cy, const char* label, bool on);
        void drawPill(int x, int y, int w, int h, const char* label, bool on);

    public:
        GameBoyPad(int x, int y, int w);

        // 今押しているボタン(GbEmu::Button のOR)
        uint8_t getPressed() const { return this->pressed; }

        void setOnRom(std::function<void()> callback){ this->on_rom = callback; }
        void setOnBack(std::function<void()> callback){ this->on_back = callback; }

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::GameBoyPad; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
