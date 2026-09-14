#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollList.hpp"

// 検索結果を出すダイアログ(PROTOCOL.md「3. 検索」)。
//
// **通信は一切しない。** 何件目から何件取るかも含めて判断するのは MarkdownScene で、
// ここは「状態の1行 + 結果の一覧 + 3つのボタン」を出すだけの入れ物にしてある。
// 検索中の進捗もここへ出すので、シーンからは setMessage() で随時書き換える。
//
// 骨格は他のダイアログと同じ(children_で子を保持 / setOnClosed で結果通知 /
// setVisible(false) で終了)。結果が選ばれた場合だけ on_select が先に呼ばれる。
class SearchDialog : public Widget {

    private:
        std::vector<Widget*> children_;

        constexpr static int DIALOG_WIDTH  = 210;
        constexpr static int DIALOG_HEIGHT = 250;

        constexpr static int BASE_X = (SCREEN_WIDTH - DIALOG_WIDTH) * 0.5;
        constexpr static int BASE_Y = (SCREEN_HEIGHT - DIALOG_HEIGHT) * 0.5;

        constexpr static int MARGIN = 5;
        constexpr static int INNER_W = DIALOG_WIDTH - MARGIN * 2;

        constexpr static int MESSAGE_H = 18;
        constexpr static int BUTTON_H = 18;
        //Buttonのボックスは setW/setH に立体ぶんが足される(getLocalRect()参照)
        constexpr static int BUTTON_EDGE = 3;

        constexpr static int LIST_Y = BASE_Y + MARGIN + MESSAGE_H;
        constexpr static int LIST_H = DIALOG_HEIGHT - MARGIN * 3 - MESSAGE_H - BUTTON_H - BUTTON_EDGE;
        constexpr static int BUTTON_Y = BASE_Y + DIALOG_HEIGHT - MARGIN - BUTTON_H - BUTTON_EDGE;

        //ScrollListのreserveへ渡すヒント。DocSearch::kMaxHits と同じ数にしてある
        constexpr static int DEFAULT_ROWS = 10;

        constexpr static int RESEARCH_W = 48; // 「再検索」
        constexpr static int NEXT_W     = 32; // 「次へ」
        constexpr static int CLOSE_W    = 48; // 「閉じる」

        Label<PICO_STR_L>* message;
        ScrollList* list;
        Button* research_button;
        Button* next_button;
        Button* close_button;

        std::function<void(int index)> on_select = nullptr;
        std::function<void()> on_research = nullptr;
        std::function<void()> on_next = nullptr;
        std::function<void(bool is_ok)> on_closed = nullptr;

        Button* makeButton(const char* text, int x, int w){
            auto* b = new Button(x, BUTTON_Y, text);
            b->setFontSize(FontFn::Small);
            b->setAllowTextSpacing(false);
            b->setW(w);
            b->setH(BUTTON_H);
            b->setParent(this);
            return b;
        }

    public:

        SearchDialog(){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->message = new Label<PICO_STR_L>(BASE_X + MARGIN, BASE_Y + MARGIN, "");
            this->message->setFontSize(FontFn::Small);
            this->message->setMaxWidth(INNER_W);
            this->message->setMaxHeight(MESSAGE_H);
            this->message->setParent(this);

            this->list = new ScrollList(BASE_X + MARGIN, LIST_Y, INNER_W, LIST_H, DEFAULT_ROWS);
            this->list->setFontSize(FontFn::Small);
            this->list->setParent(this);
            this->list->setOnSelectItem([this](int index, bool already_selected){
                //1回目のタップで選択、2回目で決定(ScrollListの流儀)
                if(!already_selected) return;
                if(this->on_select) this->on_select(index);
                this->setVisible(false);
            });

            this->research_button = makeButton("再検索", BASE_X + MARGIN, RESEARCH_W);
            this->research_button->setOnPressStart([this](){
                if(this->on_research) this->on_research();
            });

            this->next_button = makeButton("次へ",
                BASE_X + (DIALOG_WIDTH - NEXT_W - BUTTON_EDGE) / 2, NEXT_W);
            this->next_button->setOnPressStart([this](){
                if(this->on_next) this->on_next();
            });
            this->next_button->setVisible(false);

            this->close_button = makeButton("閉じる",
                BASE_X + DIALOG_WIDTH - MARGIN - CLOSE_W - BUTTON_EDGE, CLOSE_W);
            this->close_button->setOnPressStart([this](){
                if(this->on_closed) this->on_closed(false);
                this->setVisible(false);
            });

            children_.push_back(this->message);
            children_.push_back(this->list);
            children_.push_back(this->research_button);
            children_.push_back(this->next_button);
            children_.push_back(this->close_button);

            this->visible = false;
        }

        // ---- シーンから中身を書き換える口 ----

        void setMessage(const char* text){
            this->message->setText(text);
        }

        void clearResults(){
            this->list->clear();
            this->list->needsRender();
        }

        void addResult(const char* title){
            ScrollListTools::Item item;
            item.icon = IconID::File;
            item.text.assign(title);
            this->list->add(item);
        }

        // 「次へ」は続きがありそうなときだけ出す(PROTOCOL.mdは総件数を返さない)
        void setHasNext(bool value){
            this->next_button->setVisible(value);
            this->needsRender();
        }

        void setOnSelect(std::function<void(int index)> callback){ this->on_select = callback; }
        void setOnResearch(std::function<void()> callback){ this->on_research = callback; }
        void setOnNext(std::function<void()> callback){ this->on_next = callback; }
        void setOnClosed(std::function<void(bool is_ok)> callback){ this->on_closed = callback; }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::SearchDialog; }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~SearchDialog(){
            delete this->message;
            delete this->list;
            delete this->research_button;
            delete this->next_button;
            delete this->close_button;
        }
};
