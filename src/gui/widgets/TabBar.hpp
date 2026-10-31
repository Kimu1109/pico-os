#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IFontImplementation.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "gui/icons/icon_render.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 横に並んだタブから1つを選ぶウィジェット。
//
// タブ1つごとにButtonをnewせず、ラベルの固定長配列 + タップ位置からの逆算で処理する
// (AppGrid / ColorDialog / KeyboardNum と同じ方式)。子ウィジェットを持たないので
// 「表示用の子がタップを奪う」問題(Widget::hit_transparentの説明を参照)とも無縁。
//
// 選択中のタブは黒塗り+白抜き文字で表す(KeyboardNumのタブ行と同じ見た目)。
//
// タブは文字の代わりにアイコンでもよい(addTab(IconID, label))。英語など文字が長くなる言語でも幅が変わらないので、
// 絵で伝わるタブは絵にする(docs/design/i18n.md「タブ」)。アイコンは高さに合わせて24pxか16pxで描く。
class TabBar : public Widget, public IFontImplementation, public IBorderColor {
    public:
        // 同時に並べられるタブの上限。増やすとlabels[]のぶんだけsizeofが増える
        constexpr static int kMaxTabs = 8;

    private:
        // 文字と罫線の間に最低限空ける余白。折返し位置の判定にも使う
        constexpr static int kTextPadding = 2;

        // 「ストップウォッチ」(UTF-8で24B)が収まらないのでPICO_STR_S(24B)では足りない
        FixedString<PICO_STR_M> labels[kMaxTabs];
        // アイコンのタブ(icon_mask のビット=index)。アイコンのタブもラベルは持つ(getLabel() 用。描かない)
        IconID icons[kMaxTabs] = {};
        uint8_t icon_mask = 0;
        static_assert(kMaxTabs <= 8, "icon_mask のビット数が足りません");
        bool hasIcon(int index) const { return (this->icon_mask >> index) & 1; }
        IconSize iconSize() const;
        int tab_count = 0;
        int selected = 0;

        // 選択が「変わったとき」だけ呼ばれる(同じタブを押し直しても飛ばない)
        InplaceFunction<void(int index)> on_changed = nullptr;

        // 幅をタブ数で割った余りは最後のタブへ足す。
        // 均等割りのまま切り捨てると右端に隙間が残り、罫線が1本浮いて見えるため
        int tabX(int index) const;
        int tabW(int index) const;

        // setFitToLabels(true)のとき: ラベルの幅に合わせて各タブの幅を決める(fit_w[])。
        // 全部が1行で収まらないなら均等割り(従来どおり折り返す)へ戻す
        bool fit_labels = false;
        bool fit_valid = false;
        int16_t fit_w[kMaxTabs] = {};
        void relayoutFit();

        // 空白も日本語も含まない英字だけの1語か(折り返さずに横へ縮めて1行に収めるもの)
        static bool isSingleLatinWord(const char* s);
        // ラベルが1行に収まらない場合の折返し位置(先頭からのバイト数)を返す。0 = 折り返さない。
        // フォントが適用済みであること(render()の中からしか呼ばない)
        int wrapOffset(const FixedString<PICO_STR_M>& label, int budget_w) const;

    public:
        TabBar(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
            this->f_size = FontFn::Small;
        }

        // タブを末尾へ足す。上限を超えた分は無視してfalseを返す
        bool addTab(const char* label);
        // アイコンのタブを末尾へ足す。label は描かない(getLabel() で読めるだけ)
        bool addTab(IconID icon, const char* label = "");

        int getTabCount() const { return this->tab_count; }

        // index番目のラベルを書き換える(範囲外はfalse)
        bool setLabel(int index, const char* label);
        const char* getLabel(int index) const {
            return (index >= 0 && index < this->tab_count) ? this->labels[index].c_str() : nullptr;
        }
        // index番目のタブを取り除く(範囲外はfalse)。選択中より前を消したら選択が追従し、
        // 選択中を消したら範囲内へ詰める(通知はしない)
        bool removeTab(int index);
        void clearTabs();

        int getSelected() const { return this->selected; }

        // 選択を変える。notify=trueならon_changedも呼ぶ(タップ経由と同じ扱いになる)
        void setSelected(int index, bool notify = false);

        void setOnChanged(InplaceFunction<void(int index)> callback){
            this->on_changed = callback;
        }

        void causeOnPressStart() override;
        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::TabBar; }
        bool focusableByDefault() const override { return true; }
        // ←→で隣のタブへ(端ではフォーカスが隣のウィジェットへ移る)
        bool onFocusKey(FocusKey key) override {
            int next = this->selected;
            if(key == FocusKey::Left) next--;
            else if(key == FocusKey::Right) next++;
            else return key == FocusKey::Activate;
            if(next < 0 || next >= this->tab_count) return false;
            this->setSelected(next, true);
            return true;
        }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setFontSize(FontFn::FontSize size) override {
            this->f_size = size;
            this->relayoutFit();
            this->needsRender();
        }
        // タブの幅をラベルの長さに合わせる(短いラベルは狭く、長いラベルは広く)。
        // 均等割りだと「グラフ」のような長めのラベルだけが2行に折り返されてしまう場合に使う。
        // 余った幅はタブ全部へ均等に配る
        void setFitToLabels(bool on){
            this->fit_labels = on;
            this->relayoutFit();
            this->needsRender();
        }
        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }

        void setW(int w){
            this->l_rect.w = w;
            this->relayoutFit();
            this->needsRender();
        }
        void setH(int h){
            this->l_rect.h = h;
            this->needsRender();
        }
};
