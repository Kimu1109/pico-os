#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/ITextColor.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/icons/icon_render.h"
#include "util/FixedString.hpp"

namespace ScrollListTools {
    struct Item {
        IconID icon = IconID::AppBox;
        FixedString<PICO_PATH_LEN> text;
        // この項目だけの文字色(パレット番号)。-1なら一覧の text_color。選択中は反転色が優先
        int8_t color = -1;
    };
};

class ScrollList : public Widget, public IFontImplementation, public IBorderColor, public ITextColor {

    private:
        // 実体で持つ(以前はnewしたポインタだったが、デストラクタが無く
        // ScrollListを破棄するたびにリークしていた)。ヒープ確保も1回減る
        std::vector<ScrollListTools::Item> dataSource;
        int scrollY = 0;

        const static int MARGIN = 2;
        const static int SCROLL_BAR_W = 15;

        bool is_scrolling = false;
        int ref_touch_y = 0;
        int ref_scroll_y = 0;

        int font_h = 0;

        int selected_index = -1;
        bool enable_icon = false;

        std::function<void(int index, bool already_selected)> on_selectitem = nullptr;

    public:

        ScrollList(int16_t x, int16_t y, int16_t w, int16_t h, int16_t default_size = -1){
            this->l_rect = {x, y, w, h};
            if(default_size != -1){
                dataSource.reserve(default_size);
            }
        }

        void add(const ScrollListTools::Item value){
            dataSource.push_back(value);
            this->needsRender();
        }
        void clear(){
            dataSource.clear();
            this->selected_index = -1;
            this->scrollY = 0;
            this->needsRender();
        }
        // index番目の位置へ挿入する(範囲外は末尾)。選択中の項目はずれた分だけ追従する
        void insertAt(int index, const ScrollListTools::Item& value){
            if(index < 0 || index > (int)dataSource.size()) index = (int)dataSource.size();
            dataSource.insert(dataSource.begin() + index, value);
            if(this->selected_index >= index) this->selected_index++;
            this->needsRender();
        }
        // index番目を取り除く。範囲外ならfalse。選択中の項目を消したら選択は外れる
        bool removeAt(int index){
            if(index < 0 || index >= (int)dataSource.size()) return false;
            dataSource.erase(dataSource.begin() + index);
            if(this->selected_index == index) this->selected_index = -1;
            else if(this->selected_index > index) this->selected_index--;
            this->clampScroll();
            this->needsRender();
            return true;
        }
        // index番目の項目が見える位置までスクロールする(一番上に寄せる)
        void scrollToIndex(int index){
            if(index < 0 || index >= (int)dataSource.size()) return;
            if(this->font_h == 0) this->font_h = FontFn::GetFontSize(getFontSize());
            this->scrollY = index * (this->font_h + MARGIN);
            this->clampScroll();
            this->needsRender();
        }
        int getScrollY() const { return this->scrollY; }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::ScrollList; }
        bool focusableByDefault() const override { return true; }
        // ↑↓で選ぶ項目を動かす(タップの1回目と同じ扱い)、決定で選んでいる項目をもう一度タップしたことにする
        // (2回タップで開く画面がそのまま使える)。端より先はフォーカスが隣のウィジェットへ移る
        bool onFocusKey(FocusKey key) override;
        // index番目の項目が見えるように最小限スクロールする
        void ensureVisible(int index);

    private:
        // 項目数・表示領域が変わったあとにscrollYを範囲内へ戻す
        void clampScroll(){
            const int total = (this->font_h + MARGIN) * (int)this->dataSource.size();
            const int max_scroll = total > this->l_rect.h ? total - this->l_rect.h : 0;
            if(this->scrollY > max_scroll) this->scrollY = max_scroll;
            if(this->scrollY < 0) this->scrollY = 0;
        }
    public:

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        void setOnSelectItem(std::function<void(int index, bool already_selected)> on_selectitem) {
            this->on_selectitem = on_selectitem;
        }
        void causeOnSelectItem(bool already_selected){
            if(this->on_selectitem) this->on_selectitem(this->selected_index, already_selected);
        }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color){
            this->border_color = palette_color;
            this->needsRender();
        }
        void setTextColor(int8_t palette_color){
            this->text_color = palette_color;
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

        void setSelectedIndex(int index){
            this->selected_index = index;
            this->needsRender();
        }
        void clearSelectedIndex(){
            this->selected_index = -1;
        }
        int getSelectedIndex(){ return this->selected_index; }

        void setEnableIcon(bool value){
            this->enable_icon = value;
        }
        bool getEnableIcon() { return this->enable_icon; }

        ScrollListTools::Item* itemAt(int index){
            if (index < 0 || index >= this->dataSource.size()) {
                return nullptr;
            }
            return &this->dataSource.at(index);
        }

        int getItemCount() const { return (int)this->dataSource.size(); }

        int getFittingHeight(){
            if(this->font_h == 0){
                this->font_h = FontFn::GetFontSize(getFontSize());
            }
            return min(this->dataSource.size() * (this->font_h + MARGIN), SCREEN_HEIGHT - this->getScreenY());
        }      
};