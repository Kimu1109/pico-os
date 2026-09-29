#include "gui/scenes/FileViewerScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    // pathの拡張子がextと一致するか(大文字小文字を区別しない)。
    // 元は英字だけの拡張子(.md/.pimg等)しか比較しないため、FixedStringの
    // charCount()等の文字単位API(UTF-8境界考慮)は不要でstrcasecmpで足りる
    bool HasExtension(const char* path, const char* ext){
        const size_t path_len = strlen(path);
        const size_t ext_len  = strlen(ext);
        if(ext_len > path_len) return false;
        return strcasecmp(path + (path_len - ext_len), ext) == 0;
    }
}

Rect FileViewerScene::bodyRect() const {
    const Rect content = Scene::contentRect();
    const int16_t body_y = content.y + MARGIN + this->top_row_h + MARGIN;

    return {
        content.x,
        body_y,
        content.w,
        (int16_t)(content.y + content.h - body_y)
    };
}

void FileViewerScene::applyVisibility(){
    const bool browsing = (this->mode == Mode::Browse);

    //Browseモードの「戻る」はランチャへ、Viewモードの「戻る」は一覧へ(一段階だけ戻る)
    if(this->back_button) this->back_button->setText(browsing ? "戻る" : "一覧");

    if(this->status_label) this->status_label->setVisible(!browsing);
    if(this->explorer)     this->explorer->setVisible(browsing);

    if(this->md_view)      this->md_view->setVisible(!browsing && this->view_kind == ViewKind::Markdown);
    if(this->text_scroll)  this->text_scroll->setVisible(!browsing && this->view_kind == ViewKind::Text);
    if(this->image_scroll) this->image_scroll->setVisible(!browsing && this->view_kind == ViewKind::Image);
}

void FileViewerScene::showTextMessage(const char* message){
    this->text_buf.assign(message);
    this->text_label->setText(this->text_buf);
    this->text_scroll->refreshContentBounds();
    this->text_scroll->scrollToTop();
    this->view_kind = ViewKind::Text;
}

bool FileViewerScene::loadPlainText(const char* path){
    FsFile f = OSData::SD.open(path);
    if(!f) return false;

    const size_t file_size = f.fileSize();
    size_t size = file_size;
    if(size > kMaxTextBytes){
        size = kMaxTextBytes;
        //黙って切るとファイルの後半が消えた理由が分からなくなる(MarkdownView::load()と同じ配慮)
        LOG_SYS_WARN("FileViewer: %s が上限(%uB)を超えているため %uB で打ち切りました",
            path, (unsigned)kMaxTextBytes, (unsigned)file_size);
    }

    //MarkdownView::load()と同じく、ファイル全体ぶんの一時バッファをヒープへ一度に
    //要求せず、スタック上の小さなチャンクで読み進めてtext_bufへ追記する
    this->text_buf.clear();
    char chunk[256];
    size_t remaining = size;
    while(remaining > 0){
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if(got <= 0) break; //読み取り失敗。読めたところまでで打ち切る
        this->text_buf.append(chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();

    this->text_label->setText(this->text_buf);
    this->text_scroll->refreshContentBounds();
    this->text_scroll->scrollToTop();
    this->view_kind = ViewKind::Text;
    return true;
}

void FileViewerScene::openFile(const char* path){
    this->mode = Mode::View;
    this->status_label->setText(PICO_IO::filename(path));

    if(HasExtension(path, ".md") || HasExtension(path, ".markdown")){
        if(this->md_view->load(path)){
            this->view_kind = ViewKind::Markdown;
        }else{
            this->showTextMessage("このファイルを開けませんでした");
        }
    }else if(HasExtension(path, ".pimg")){
        this->image_view->setPath(path);
        //Image::updatePath()はヘッダを読めた場合だけ幅/高さを入れる(読めなければ0のまま。
        //Image.cppの修正で前回開いた画像の大きさを引きずらないようにしてある)
        if(this->image_view->getLocalRect().w > 0){
            this->image_scroll->refreshContentBounds();
            this->image_scroll->scrollToTop();
            this->view_kind = ViewKind::Image;
        }else{
            this->showTextMessage("この画像を開けませんでした");
        }
    }else{
        if(!this->loadPlainText(path)){
            this->showTextMessage("このファイルを開けませんでした");
        }
    }

    this->applyVisibility();
}

void FileViewerScene::onEnter(){
    const Rect content = Scene::contentRect();

    this->back_button = new Button(content.x + MARGIN, content.y + MARGIN, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([this](){
        if(this->mode == Mode::View){
            this->mode = Mode::Browse;
            this->applyVisibility();
        }else{
            SceneFunctions::Pop();
        }
    });
    WidgetFunctions::Add(this->back_button);

    const Rect back_box = this->back_button->getLocalRect();
    this->top_row_h = back_box.h;

    const int status_x = back_box.x + back_box.w + MARGIN;
    const int status_w = content.x + content.w - MARGIN - status_x;

    this->status_label = new Label<PICO_STR_L>(status_x, content.y + MARGIN, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(status_w);
    this->status_label->setMaxHeight(this->top_row_h);
    //ファイル名やエラー文言に**等が含まれても装飾として解釈しない
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    const Rect body = this->bodyRect();

    this->explorer = new FileExplorer(body.x, body.y, body.w, body.h);
    this->explorer->setOnFileTap([this](const char* path){
        this->openFile(path);
    });
    WidgetFunctions::Add(this->explorer);

    this->md_view = new MarkdownView(body.x, body.y, body.w, body.h);
    WidgetFunctions::Add(this->md_view);

    this->text_scroll = new ScrollContainer(body.x, body.y, body.w, body.h);
    this->text_label = new Label<kMaxTextBytes>(TEXT_PADDING, TEXT_PADDING, "");
    this->text_label->setFontSize(FontFn::Small);
    //ソースをそのまま見せるビューなので**や~をマークアップとして解釈しない
    this->text_label->setDisableAutoTextDecoration(true);
    this->text_label->setMaxWidth(body.w - SCROLLBAR_W - TEXT_PADDING * 2);
    this->text_scroll->add(this->text_label); //所有権はtext_scrollへ移る
    WidgetFunctions::Add(this->text_scroll);

    this->image_scroll = new ScrollContainer(body.x, body.y, body.w, body.h);
    this->image_scroll->setScrollAxes(true, true); //画像は横にも縦にもはみ出しうる
    this->image_view = new Image("", 0, 0, false); //空パス=初期状態は何も描かない(openFile()で差し替える)
    this->image_scroll->add(this->image_view); //所有権はimage_scrollへ移る
    WidgetFunctions::Add(this->image_scroll);

    this->mode = Mode::Browse;
    this->view_kind = ViewKind::None;
    this->applyVisibility();
}

void FileViewerScene::onExit(){
    this->back_button   = nullptr;
    this->status_label  = nullptr;
    this->explorer      = nullptr;
    this->md_view        = nullptr;
    this->text_scroll   = nullptr;
    this->text_label    = nullptr;
    this->image_scroll  = nullptr;
    this->image_view    = nullptr;
    this->mode      = Mode::Browse;
    this->view_kind = ViewKind::None;
}
