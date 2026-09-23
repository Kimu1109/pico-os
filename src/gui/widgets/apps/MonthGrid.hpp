#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "consts.hpp"

#include <cstdint>
#include <functional>

// カレンダーアプリの月表示(曜日の見出し1行 + 日付の格子6行)。
//
// 子ウィジェットは持たずrender()でframeへ直接描き、タップ位置から日付を逆算する
// (AppGrid / TabBar / DurationPicker と同じ方式)。42マスぶんButtonをnewしないため。
//
// 予定そのものは知らない。シーン側が setMonth() で月を、setCounts() で
// 「日ごとの予定の件数」を流し込む(AnalogClock が時刻を自分で取りに行かないのと同じ理由。
// Ical への依存をウィジェットへ持たせない)。
//
// 見た目:
//   - 日曜は赤、土曜は青
//   - 今日は赤の二重枠、選択中の日は黒塗り+白抜き文字
//   - 予定のある日は数字の下に点(最大3つ。件数がそれ以上でも3つ)
class MonthGrid : public Widget, public IBorderColor {
    public:
        constexpr static int kRows = 6;
        constexpr static int kCols = 7;
        // 曜日の見出しの行の高さ(16pxフォント + 余白)
        constexpr static int kHeaderH = 18;

    private:
        int year = 1970;
        int month = 1;
        int first_wday = 0;    // 1日の曜日(0=日曜)
        int days_in_month = 31;

        int today = 0;         // 表示中の月の中での今日(1始まり)。0 = この月には無い
        int selected = 0;      // 選択中の日(1始まり)。0 = 選択なし

        uint8_t counts[32] = {}; // [日] = 予定の件数。[0]は使わない

        std::function<void(int day)> on_select_day = nullptr;

        int cellX(int col) const;
        int cellW(int col) const;
        int cellH() const;

        // 画面座標から日付(1始まり)。この月の日付でなければ 0
        int dayAt(int screen_x, int screen_y) const;

    public:
        MonthGrid(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
        }

        // 表示する月を変える。今日・選択・件数はそのまま残るので、呼び出し側で入れ直すこと
        void setMonth(int year, int month);
        int getYear() const  { return this->year; }
        int getMonth() const { return this->month; }
        int getDaysInMonth() const { return this->days_in_month; }

        void setToday(int day);
        void setSelected(int day);
        int getSelected() const { return this->selected; }

        // counts[1..days_in_month] を写す(counts[0]は見ない)
        void setCounts(const uint8_t* counts_by_day);

        // タップで日付が選ばれたとき(同じ日を押し直しても呼ぶ)
        void setOnSelectDay(std::function<void(int day)> callback){
            this->on_select_day = callback;
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::MonthGrid; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }
};
