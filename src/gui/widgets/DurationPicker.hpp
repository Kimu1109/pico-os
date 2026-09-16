#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "consts.hpp"

#include <cstdint>
#include <functional>

// 「時:分:秒」を大きく表示する時間の表示/入力欄。
//
// 子ウィジェットは持たずrender()でframeへ直接描く(AnalogClock / AppGrid / TabBar と同じ方式)。
// 桁ごとにButtonをnewせず、押された座標から「どの桁の▲/▼か」を逆算する。
//
// 保持するのはミリ秒の総量だけで、時/分/秒は描くときに割り出す。
// タイマーの「設定値」と「残り時間」を同じウィジェットで見せられるようにするためで、
// 編集させたくない場面(カウントダウン中)は setEditable(false) で▲▼を消す。
//
// 時刻を自分では取りに行かない(TimeFunctionsへ依存しない)のはAnalogClockと同じ方針。
class DurationPicker : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {
    public:
        // 表示できる上限。桁あふれで表示が崩れないよう 23:59:59 で頭打ちにする
        constexpr static uint32_t kMaxSeconds = 23u * 3600u + 59u * 60u + 59u;
        constexpr static uint32_t kMaxMs      = kMaxSeconds * 1000u;

    private:
        uint32_t total_ms = 0;
        bool editable = true;

        // 値が▲▼で変わったときだけ呼ばれる(setTotalMs()では呼ばれない)。
        // setTotalMs()でも飛ばすと、シーン側が表示を流し込むたびに再入する
        std::function<void(uint32_t total_ms)> on_changed = nullptr;

        // ---- 長押しの連続加算 ----
        // 25分を1タップずつ積むのは現実的でないので、押しっぱなしで進み続けるようにする
        int repeat_field = -1; // -1 = 押していない
        int repeat_step  = 0;  // +1 / -1
        unsigned long repeat_started_ms = 0;
        unsigned long repeat_last_ms    = 0;

        constexpr static int kArrowH   = 16; // ▲▼の高さ
        constexpr static int kArrowW   = 28; // ▲▼の底辺
        constexpr static int kArrowGap = 8;  // 数字と▲▼の間
        constexpr static int kTouchPad = 8;  // 当たり判定を▲▼の外側へ広げる余白

        constexpr static unsigned long kRepeatDelayMs    = 450;
        constexpr static unsigned long kRepeatIntervalMs = 110;

        // 描画と当たり判定で同じ数値を使うための実測レイアウト(いずれもスクリーン座標)。
        // 文字幅はフォント依存なので定数では持てず、その都度測る
        struct Layout {
            int col_x[3]; // 時/分/秒それぞれの左端
            int col_w;    // 2桁ぶんの幅
            int colon_w;  // ":" の幅
            int digits_y; // 数字の上端
            int digits_h;
            int arrow_up_y;   // ▲の上端
            int arrow_down_y; // ▼の上端
        };

        Layout computeLayout();

        // 押された座標が入る桁を返す(▲なら step_out=+1 / ▼なら -1)。外れていれば -1
        int hitField(int px, int py, int& step_out);

        // 桁を1つ進める/戻す。繰り上がりはせず、その桁だけが巡回する
        void applyStep(int field, int step);

        // 長押し中の連続加算。render()から毎フレーム呼ばれる
        void tickRepeat();

        // 表示する秒数。カウントダウンでは端数を切り上げる
        // (残り0.4秒を「00:00:00」と出すと、まだ鳴っていないのに終わったように見えるため)
        uint32_t displaySeconds() const;

    public:
        DurationPicker(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
            this->f_size = FontFn::Bigger;
        }

        void causeOnPressStart() override;
        void causeOnPressEnd() override;

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::DurationPicker; }

        // 背景は自分の矩形を塗り潰す前提。OPAQUEにしておくと、数字が変わるたびのdirty矩形で
        // 下のウィジェットを描き直さずに済む(AnalogClockと同じ理由)
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        uint32_t getTotalMs() const { return this->total_ms; }
        uint32_t getTotalSeconds() const { return this->total_ms / 1000u; }

        // 値を流し込む。on_changedは呼ばれない
        void setTotalMs(uint32_t ms);
        void setTotalSeconds(uint32_t sec){ this->setTotalMs(sec * 1000u); }

        bool getEditable() const { return this->editable; }

        // ▲▼の表示/非表示。数字の位置も変わる(▲▼のぶんだけ縦に詰まる)
        void setEditable(bool v);

        void setOnChanged(std::function<void(uint32_t total_ms)> callback){
            this->on_changed = callback;
        }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color) override {
            this->text_color = palette_color;
            this->needsRender();
        }

        void setW(int w){
            this->l_rect.w = (int16_t)w;
            this->needsRender();
        }
        void setH(int h){
            this->l_rect.h = (int16_t)h;
            this->needsRender();
        }
};
