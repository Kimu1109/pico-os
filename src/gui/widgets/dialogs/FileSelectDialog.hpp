#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/apps/FileExplorer.hpp"
#include "gui/widgets/Button.hpp"
#include "system/I18n_Functions.hpp"

class FileSelectDialog : public Widget {
    private:
        WidgetList children_;

        InplaceFunction<void(bool is_ok)> on_close = nullptr; 

        constexpr static int BASE_X = 20;
        constexpr static int BASE_Y = 20;

        constexpr static int DIALOG_H = SCREEN_HEIGHT - BASE_Y * 2;  // 240x320で280
        constexpr static int DIALOG_W = SCREEN_WIDTH - BASE_X * 2;   // 240x320で200

        constexpr static int BUTTON_H = 26;

        constexpr static int EXPLORER_H = DIALOG_H - BUTTON_H;

        constexpr static int BUTTON_Y = BASE_Y + EXPLORER_H;
        
        constexpr static int BUTTON_MARGIN = 2;
        constexpr static int BUTTON_W = DIALOG_W / 2 - BUTTON_MARGIN * 2 - 3;

        FileExplorer* explorer;
        Button* button_ok;
        Button* button_no;

    public:
        FileSelectDialog(const char* path){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->explorer = new FileExplorer(BASE_X, BASE_Y, DIALOG_W, EXPLORER_H);
            this->explorer->setCurrentFolderPath(path);

            this->button_ok = new Button(BASE_X + DIALOG_W / 2 + BUTTON_MARGIN, BUTTON_Y - 3, "OK");
            this->button_ok->setAllowTextSpacing(false);
            this->button_ok->setFontSize(FontFn::Small);
            this->button_ok->setW(BUTTON_W + Button::kFrameExtraTight);
            this->button_ok->setH(BUTTON_H - 2 + Button::kFrameExtraTight);
            this->button_ok->setOnPressStart([this](){
                if(this->on_close) this->on_close(true);
                this->setVisible(false);
            });

            this->button_no = new Button(BASE_X + BUTTON_MARGIN, BUTTON_Y - 3, TR("キャンセル"));
            this->button_no->setAllowTextSpacing(false);
            this->button_no->setFontSize(FontFn::Small);
            this->button_no->setW(BUTTON_W + Button::kFrameExtraTight);
            this->button_no->setH(BUTTON_H - 2 + Button::kFrameExtraTight);
            this->button_no->setOnPressStart([this](){
                if(this->on_close) this->on_close(false);
                this->setVisible(false);
            });

            this->setVisible(false);

            //親を辿れるように(フォーカスの範囲・Esc/Enterをダイアログへ届ける。位置は親が画面全体なので変わらない)
            this->explorer->setParent(this);
            this->button_ok->setParent(this);
            this->button_no->setParent(this);
            children_.push_back(this->explorer);
            children_.push_back(this->button_ok);
            children_.push_back(this->button_no);
        }

        const WidgetList& getChildren() const override {
            return children_;
        }

        void setOnClose(InplaceFunction<void(bool is_ok)> callback){
            this->on_close = callback;
        }

        const char* getSelectedPath();

        // Esc(Back)でキャンセル。ボタンを押したのと同じ
        bool onFocusKey(FocusKey key) override;
        // 開いたらファイルの一覧から(読む順の最初は「上のフォルダへ」のアイコン)
        Widget* initialFocus() override { return this->explorer->getList(); }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::FileSelectDialog; }

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        ~FileSelectDialog(){
            delete this->explorer;
            delete this->button_ok;
            delete this->button_no;
        }
};