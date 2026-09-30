#pragma once

#include "gui/widgets/Widget.hpp"

#include <functional>

// 端末用の補助キーの列(SSHアプリ専用)。オンスクリーンキーボードに無いキーを並べる:
//   Esc / Tab / Ctrl(次の1文字を制御文字にする。押すと点灯) / ↑ ↓ ← → / ^C
// 「子を持たずrender()で直接描き、タップ位置から逆算する」型(TabBarと同じ)。
class TermKeyBar : public Widget {
    public:
        enum Key : uint8_t { Esc, Tab, Ctrl, Up, Down, Left, Right, CtrlC, KeyCount };

        TermKeyBar(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = { x, y, w, h };
            this->background_color = PICO_LIGHTGREY;
        }

        void setOnKey(std::function<void(Key)> fn){ this->on_key = fn; }
        void setCtrl(bool on){
            if(on == this->ctrl) return;
            this->ctrl = on;
            this->needsRender();
        }
        bool getCtrl() const { return this->ctrl; }

        void causeOnPressStart() override;
        void causeOnPressEnd() override;
        void causeOnPressOut() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::TermKeyBar; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

    private:
        std::function<void(Key)> on_key = nullptr;
        bool ctrl = false;
        int pressed = -1;

        int keyAt(int local_x) const;
        int keyX(int i) const { return i * this->l_rect.w / KeyCount; }
};
