#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/ScrollList.hpp"

// 周辺のWi-Fiをスキャンし、一覧から選んで接続するダイアログ(SettingsScene専用)。
//
// 骨格はSearchDialogと同じ(状態1行 + ScrollList + ボタン群、2回タップで選ぶ流儀)。
// **スキャンそのものはしない** — SearchDialogが検索そのものをしないのと同じ理由で、
// NetworkFunctions::ScanAsync()を呼んで結果をこのダイアログへ詰めるのはSettingsScene側の仕事。
class WifiScanDialog : public Widget {

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

        //ScrollListのreserveへ渡すヒント。NetworkScan::kMaxResultsと同じ数にしてある
        constexpr static int DEFAULT_ROWS = 16;

        constexpr static int RESCAN_W = 72; // 「再スキャン」
        constexpr static int CLOSE_W  = 48; // 「閉じる」

        Label<PICO_STR_L>* message;
        ScrollList* list;
        Button* rescan_button;
        Button* close_button;

        std::function<void(const char* ssid)> on_select = nullptr;
        std::function<void()> on_rescan = nullptr;
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

        // 電波強度アイコンはステータスバーと同じ4段階(NetworkFunctions::GetWifiStateIconID()参照)
        static IconID IconForRssi(int32_t rssi){
            if (rssi >= -50) return IconID::WifiSignal4;
            if (rssi >= -65) return IconID::WifiSignal3;
            if (rssi >= -80) return IconID::WifiSignal2;
            return IconID::WifiSignal1;
        }

    public:

        WifiScanDialog(){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->message = new Label<PICO_STR_L>(BASE_X + MARGIN, BASE_Y + MARGIN, "");
            this->message->setFontSize(FontFn::Small);
            this->message->setMaxWidth(INNER_W);
            this->message->setMaxHeight(MESSAGE_H);
            this->message->setParent(this);

            this->list = new ScrollList(BASE_X + MARGIN, LIST_Y, INNER_W, LIST_H, DEFAULT_ROWS);
            this->list->setFontSize(FontFn::Small);
            this->list->setEnableIcon(true);
            this->list->setParent(this);
            this->list->setOnSelectItem([this](int index, bool already_selected){
                //1回目のタップで選択、2回目で決定(ScrollListの流儀。SearchDialogと同じ)
                if(!already_selected) return;
                ScrollListTools::Item* item = this->list->itemAt(index);
                if(item && this->on_select) this->on_select(item->text.c_str());
                this->setVisible(false);
            });

            this->rescan_button = makeButton("再スキャン", BASE_X + MARGIN, RESCAN_W);
            this->rescan_button->setOnPressStart([this](){
                if(this->on_rescan) this->on_rescan();
            });

            this->close_button = makeButton("閉じる",
                BASE_X + DIALOG_WIDTH - MARGIN - CLOSE_W - BUTTON_EDGE, CLOSE_W);
            this->close_button->setOnPressStart([this](){
                if(this->on_closed) this->on_closed(false);
                this->setVisible(false);
            });

            children_.push_back(this->message);
            children_.push_back(this->list);
            children_.push_back(this->rescan_button);
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

        void addResult(const char* ssid, int32_t rssi){
            ScrollListTools::Item item;
            item.icon = IconForRssi(rssi);
            item.text.assign(ssid);
            this->list->add(item);
        }

        void setOnSelect(std::function<void(const char* ssid)> callback){ this->on_select = callback; }
        void setOnRescan(std::function<void()> callback){ this->on_rescan = callback; }
        void setOnClosed(std::function<void(bool is_ok)> callback){ this->on_closed = callback; }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::WifiScanDialog; }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        ~WifiScanDialog(){
            delete this->message;
            delete this->list;
            delete this->rescan_button;
            delete this->close_button;
        }
};
