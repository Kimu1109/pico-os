#include "gui/scenes/FileViewerScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

namespace {
    // pathの拡張子がextと一致するか(大文字小文字を区別しない)
    bool HasExtension(const char* path, const char* ext){
        const size_t path_len = strlen(path);
        const size_t ext_len  = strlen(ext);
        if(ext_len > path_len) return false;
        return strcasecmp(path + (path_len - ext_len), ext) == 0;
    }
}

Rect FileViewerScene::bodyRect(int top_row_h) const {
    const Rect content = Scene::contentRect();
    const int16_t body_y = content.y + MARGIN + top_row_h + MARGIN;
    return { content.x, body_y, content.w, (int16_t)(content.y + content.h - body_y) };
}

void FileViewerScene::showText(const Rect& body, const char* message){
    this->text_buf.assign(message);
    if(!this->text_view){
        this->text_view = new TextView(body.x, body.y, body.w, body.h);
        WidgetFunctions::Add(this->text_view);
    }
    this->text_view->setDocument(this->text_buf.c_str(), (int)this->text_buf.length());
}

bool FileViewerScene::loadPlainText(const Rect& body, bool& truncated){
    truncated = false;
    FsFile f = OSData::SD.open(this->path.c_str());
    if(!f) return false;

    //ファイル全体ぶんの一時バッファを確保せず、小さなチャンクで読み進めて本文へ追記する。
    //'\r'は捨てる(TextViewは'\n'だけを改行として扱う)
    this->text_buf.clear();
    char chunk[256];
    char clean[256];
    for(;;){
        const int got = f.read((uint8_t*)chunk, sizeof(chunk));
        if(got <= 0) break;
        int n = 0;
        for(int i = 0; i < got; i++){
            if(chunk[i] != '\r' && chunk[i] != '\0') clean[n++] = chunk[i];
        }
        if(this->text_buf.length() + (size_t)n > FixedString<kMaxTextBytes>::capacity()){
            //入るところまで入れて打ち切る(appendがUTF-8の文字の途中では切らない)
            this->text_buf.append(clean, (size_t)n);
            truncated = true;
            break;
        }
        this->text_buf.append(clean, (size_t)n);
    }
    f.close();

    this->text_view = new TextView(body.x, body.y, body.w, body.h);
    WidgetFunctions::Add(this->text_view);
    this->text_view->setDocument(this->text_buf.c_str(), (int)this->text_buf.length());
    if(this->text_view->isTruncated()) truncated = true;
    return true;
}

void FileViewerScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    const int status_x = back_box.x + back_box.w + MARGIN;

    this->status_label = new Label<PICO_STR_L>(status_x, content.y + MARGIN, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.x + content.w - MARGIN - status_x);
    this->status_label->setMaxHeight(back_box.h);
    //ファイル名に**等が含まれても装飾として解釈しない
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    const Rect body = this->bodyRect(back_box.h);
    const char* p = this->path.c_str();
    const char* name = PICO_IO::filename(p);
    this->status_label->setText(name);

    if(!OSData::SD_usable){
        this->showText(body, "SDカードが使えません");
        return;
    }

    if(HasExtension(p, ".md") || HasExtension(p, ".markdown")){
        this->md_view = new MarkdownView(body.x, body.y, body.w, body.h);
        WidgetFunctions::Add(this->md_view);
        if(!this->md_view->load(p)){
            this->md_view->setVisible(false);
            this->showText(body, "このファイルを開けませんでした");
        }
    }else if(HasExtension(p, ".pimg")){
        this->image_view = new ImageView(body.x, body.y, body.w, body.h);
        WidgetFunctions::Add(this->image_view);
        if(!this->image_view->load(p)){
            this->image_view->setVisible(false);
            this->showText(body, "この画像を開けませんでした");
        }else{
            char buf[PICO_STR_L];
            snprintf(buf, sizeof(buf), "%s %dx%d", name,
                this->image_view->getImageW(), this->image_view->getImageH());
            this->status_label->setText(buf);
        }
    }else{
        bool truncated = false;
        if(!this->loadPlainText(body, truncated)){
            this->showText(body, "このファイルを開けませんでした");
        }else if(truncated){
            char buf[PICO_STR_L];
            snprintf(buf, sizeof(buf), "%s(途中まで)", name);
            this->status_label->setText(buf);
            LOG_SYS_WARN("FileViewer: %s は大きすぎるため途中までしか表示しません", p);
        }
    }
}

void FileViewerScene::onExit(){
    this->back_button  = nullptr;
    this->status_label = nullptr;
    this->md_view      = nullptr;
    this->text_view    = nullptr;
    this->image_view   = nullptr;
}
