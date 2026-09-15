#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 横に並んだタブから1つを選ぶウィジェット。
//
// タブ1つごとにButtonをnewせず、ラベルの固定長配列 + タップ位置からの逆算で処理する
// (AppGrid / ColorDialog / KeyboardNum と同じ方式)。子ウィジェットを持たないので
// 「表示用の子がタップを奪う」問題(Widget::hit_transparentの説明を参照)とも無縁。
//
// 選択中のタブは黒塗り+白抜き文字で表す(KeyboardNumのタブ行と同じ見た目)。
class TabBar : public Widget, public IFontImplementation, public IBorderColor {
    public:
        // 同時に並べられるタブの上限。増やすとlabels[]のぶんだけsizeofが増える
        constexpr static int kMaxTabs = 4;

    private:
        FixedString<PICO_STR_S> labels[kMaxTabs];
        int tab_count = 0;
        int selected = 0;

        // 選択が「変わったとき」だけ呼ばれる(同じタブを押し直しても飛ばない)
        std::function<void(int index)> on_changed = nullptr;

        // 幅をタブ数で割った余りは最後のタブへ足す。
        // 均等割りのまま切り捨てると右端に隙間が残り、罫線が1本浮いて見えるため
        int tabX(int index) const;
        int tabW(int index) const;

    public:
        TabBar(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
            this->f_size = FontFn::Small;
        }

        // タブを末尾へ足す。上限を超えた分は無視してfalseを返す
        bool addTab(const char* label);

        int getTabCount() const { return this->tab_count; }

        int getSelected() const { return this->selected; }

        // 選択を変える。notify=trueならon_changedも呼ぶ(タップ経由と同じ扱いになる)
        void setSelected(int index, bool notify = false);

        void setOnChanged(std::function<void(int index)> callback){
            this->on_changed = callback;
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::TabBar; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }

        void setW(int w){
            this->l_rect.w = w;
            this->needsRender();
        }
        void setH(int h){
            this->l_rect.h = h;
            this->needsRender();
        }
};
