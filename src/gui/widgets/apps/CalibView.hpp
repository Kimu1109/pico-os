#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

// タッチ補正の画面(TouchCalibScene)の背景。基準の点(十字)と、確認用の「今のタッチ位置」の
// 点を描くだけの表示専用の部品。タッチには反応しない(シーンが OSData を直接読む)。
class CalibView : public Widget {
    public:
        static constexpr int kMaxMarks = 8;

        CalibView(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
            this->hit_transparent = true;
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::CalibView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        // 基準の点(画面座標)を全部消す
        void clearMarks();
        // 基準の点を足す。done=trueは入力済みの小さな灰色の十字
        int addMark(int x, int y);
        void setMarkDone(int index, bool done);
        // 今狙わせる点(-1でなし。太い赤の十字を描く)
        void setActive(int index);
        // 確認用の点(補正後のタッチ位置)。show=falseで消す
        void setProbe(bool show, int x, int y);

    private:
        struct Mark { int16_t x, y; bool done; };
        Mark marks[kMaxMarks] = {};
        int mark_count = 0;
        int active = -1;
        bool probe_show = false;
        int16_t probe_x = 0, probe_y = 0;
};
