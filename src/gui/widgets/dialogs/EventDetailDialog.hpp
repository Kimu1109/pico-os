#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollContainer.hpp"

// カレンダーの予定1件の詳細(題名 + 日時・場所・説明文)を出すダイアログ。
//
// 中身を作るのは CalendarScene で、ここは「題名の行 + スクロールする本文 + 閉じる」を
// 出すだけの入れ物(SearchDialog と同じ考え方)。本文は説明文(DESCRIPTION)まで入ると
// 長くなるので、DictScene の詳細欄と同じく ScrollContainer で包んで全文を読めるようにする。
//
// 骨格は他のダイアログと同じ(children_で子を保持 / setOnClosed で結果通知 /
// setVisible(false) で終了)。
class EventDetailDialog : public Widget {
    public:
        // 本文の上限。題名以外(日時・場所・カレンダー名・説明文)を全部入れる
        using Body = FixedString<PICO_STR_1KiB>;

    private:
        std::vector<Widget*> children_;

        constexpr static int DIALOG_WIDTH  = 224;
        constexpr static int DIALOG_HEIGHT = 280;

        constexpr static int BASE_X = (SCREEN_WIDTH - DIALOG_WIDTH) / 2;
        constexpr static int BASE_Y = STATUSBAR_HEIGHT + (SCREEN_HEIGHT - STATUSBAR_HEIGHT - DIALOG_HEIGHT) / 2;

        constexpr static int MARGIN = 5;
        constexpr static int INNER_W = DIALOG_WIDTH - MARGIN * 2;

        // 題名は2行まで(16px x 2)
        constexpr static int TITLE_H = 34;
        constexpr static int BUTTON_H = 18;
        //Buttonのボックスは setW/setH に立体ぶんが足される(SearchDialogと同じ)
        constexpr static int BUTTON_EDGE = 3;
        constexpr static int CLOSE_W = 48; // 「閉じる」

        constexpr static int BODY_Y = BASE_Y + MARGIN + TITLE_H + MARGIN;
        constexpr static int BUTTON_Y = BASE_Y + DIALOG_HEIGHT - MARGIN - BUTTON_H - BUTTON_EDGE;
        constexpr static int BODY_H = BUTTON_Y - MARGIN - BODY_Y;

        // ScrollContainer の縦スクロールバー(ScrollContainer::SCROLL_L と一致させること)と余白
        constexpr static int BODY_SCROLLBAR_W = 15;
        constexpr static int BODY_PADDING = 2;

        Label<PICO_STR_L>* title;
        ScrollContainer* body_scroll;
        Label<PICO_STR_1KiB>* body;   // 所有権は body_scroll にある
        Button* close_button;

        std::function<void(bool is_ok)> on_closed = nullptr;

    public:
        EventDetailDialog();
        ~EventDetailDialog();

        void setContent(const char* title_text, const char* body_text);

        void setOnClosed(std::function<void(bool is_ok)> callback){ this->on_closed = callback; }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::EventDetailDialog; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override { return children_; }
};
