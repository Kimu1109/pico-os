#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"

class KeyboardPanel;
class ITextInputWidget;

// オンスクリーンキーボードの「ダイアログ枠」。
// 画面全体に背景の斜線を掛けて下のウィジェットへのタップを塞ぎ、上部に入力中の文字列を出す。
// キー盤(KeyboardPanel)はこの上へ重なるので、ここではキーを一切扱わない。
// OS常駐のオーバーレイ(KeyboardFunctions::Setup()が作る)で、表示/非表示もKeyboardFunctionsが決める。
class KeyboardDialog : public Widget {
    private:
        std::vector<Widget*> children_;
        Label<PICO_STR_LL>* preview;

        static constexpr int MARGIN = 10;

        int panel_top = SCREEN_HEIGHT;
        // キー盤が低いものへ切り替わったとき、それまでキー盤が覆っていた所には
        // 旧キー盤の絵が残っている。TRANSLUCENTのままだと下が描き直されないので、
        // この値が残っている間だけCLEARとして振る舞い、下の画面ごと描き直させる。
        // UpdateAll()中のrender()(毎フレーム、FlushDirty()より前)で減らすので、2で「このフレームだけ」になる
        int redraw_below_frames = 0;

    public:
        KeyboardDialog();
        ~KeyboardDialog() override { delete this->preview; }

        // キー盤の上端に合わせて入力欄の高さを決め直す
        void attach(KeyboardPanel* panel);
        // キー盤の中身(テキスト・カーソル・変換中の読み)を入力欄へ反映する
        void refresh(ITextInputWidget* keyboard);

        void render() override;
        void setVisible(bool visible) override {
            if(!visible) this->panel_top = SCREEN_HEIGHT;
            Widget::setVisible(visible);
        }

        const std::vector<Widget*>& getChildren() const override { return children_; }

        WidgetType getWidgetType() const override { return WidgetType::KeyboardDialog; }
        WidgetTools::RenderMode getRenderMode() const override {
            return this->redraw_below_frames > 0 ? WidgetTools::CLEAR : WidgetTools::TRANSLUCENT;
        }
};
