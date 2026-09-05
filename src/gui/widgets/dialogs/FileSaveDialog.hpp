#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/FileExplorer.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Textbox.hpp"

class FileSaveDialog : public Widget {
    private:
        std::vector<Widget*> children_;

        constexpr static int BASE_X = 20;
        constexpr static int BASE_Y = 20;

        constexpr static int DIALOG_H = 280;
        constexpr static int DIALOG_W = 200;

        constexpr static int TEXTBOX_H = 26;
        constexpr static int BUTTON_H = 26;

        constexpr static int EXPLORER_H = DIALOG_H - TEXTBOX_H - BUTTON_H;

        constexpr static int TEXTBOX_Y = BASE_Y + EXPLORER_H;
        constexpr static int BUTTON_Y = TEXTBOX_Y + TEXTBOX_H;
        
        constexpr static int BUTTON_MARGIN = 2;
        constexpr static int BUTTON_W = DIALOG_W / 2 - BUTTON_MARGIN * 2 - 3;

        FileExplorer* explorer;
        Button* button_ok;
        Button* button_no;
        Textbox* textbox_filename;

    public:
        FileSaveDialog(const char* path){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};

            this->explorer = new FileExplorer(BASE_X, BASE_Y, DIALOG_W, EXPLORER_H);
            this->explorer->setCurrentFolderPath(path);

            this->textbox_filename = new Textbox("", BASE_X, TEXTBOX_Y, DIALOG_W, TEXTBOX_H, true);
            this->textbox_filename->setPlaceholder("ファイル名を入力...");
            this->textbox_filename->setFontSize(FontFn::Small);
            this->textbox_filename->setMaxHeight(TEXTBOX_H);

            this->button_ok = new Button(BASE_X + DIALOG_W / 2 + BUTTON_MARGIN, BUTTON_Y - 3, "OK");
            this->button_ok->setAllowTextSpacing(false);
            this->button_ok->setFontSize(FontFn::Small);
            this->button_ok->setW(BUTTON_W);
            this->button_ok->setH(BUTTON_H - 2);

            this->button_no = new Button(BASE_X + BUTTON_MARGIN, BUTTON_Y - 3, "キャンセル");
            this->button_no->setAllowTextSpacing(false);
            this->button_no->setFontSize(FontFn::Small);
            this->button_no->setW(BUTTON_W);
            this->button_no->setH(BUTTON_H - 2);

            children_.push_back(this->explorer);
            children_.push_back(this->textbox_filename);
            children_.push_back(this->button_ok);
            children_.push_back(this->button_no);
        }

        const std::vector<Widget*>& getChildren() const override {
            return children_;
        }

        void render() override;

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::TRANSLUCENT; }

        ~FileSaveDialog(){
            delete this->explorer;
            delete this->textbox_filename;
            delete this->button_ok;
            delete this->button_no;
        }
};