#include "gui/scenes/MarkdownScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "OS_Data.hpp"

#include <cstring>

void MarkdownScene::onEnter(){
    const Rect content = Scene::contentRect();

    const int header_y = content.y + (HEADER_H - BUTTON_H) / 2 - 2;
    const int view_y   = content.y + HEADER_H;
    const int view_h   = content.h - HEADER_H - FOOTER_H;

    // ---- ヘッダ ----
    // 「<」「>」はHomeSceneのページ送りと同じ流儀(アイコンではなく素のButton)。
    // Buttonはアイコンを持てないため、フォントに確実に含まれるASCIIで済ませている
    this->back_button = new Button(MARGIN, header_y, "<");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setAllowTextSpacing(false);
    this->back_button->setW(NAV_BUTTON_W);
    this->back_button->setH(BUTTON_H);
    this->back_button->setOnPressEnd([this](){ this->goBack(); });
    WidgetFunctions::Add(this->back_button);

    this->forward_button = new Button(MARGIN + NAV_BUTTON_W + 8, header_y, ">");
    this->forward_button->setFontSize(FontFn::Small);
    this->forward_button->setAllowTextSpacing(false);
    this->forward_button->setW(NAV_BUTTON_W);
    this->forward_button->setH(BUTTON_H);
    this->forward_button->setOnPressEnd([this](){ this->goForward(); });
    WidgetFunctions::Add(this->forward_button);

    this->exit_button = new Button(content.w - MARGIN - EXIT_BUTTON_W, header_y, "終了");
    this->exit_button->setFontSize(FontFn::Small);
    this->exit_button->setAllowTextSpacing(false);
    this->exit_button->setW(EXIT_BUTTON_W);
    this->exit_button->setH(BUTTON_H);
    this->exit_button->setOnPressEnd([](){
        //ここでのPopはアプリ終了(ランチャへ戻る)。文書の履歴とは別物
        SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->exit_button);

    // ---- 本文 ----
    this->view = new MarkdownView(0, view_y, content.w, view_h);
    this->view->setOnLinkTap([this](FixedString<PICO_PATH_LEN> url){
        this->onLinkTap(url);
    });
    WidgetFunctions::Add(this->view);

    // ---- フッタ ----
    // 幅いっぱいに1行だけ出す。max_widthで折り返させたうえでmax_heightを1行に
    // 絞ることで、長いパスは右側が表示されなくなる(ブラウザのURL欄と同じ見え方)
    this->status_label = new Label<PICO_PATH_LEN>(MARGIN, content.y + content.h - FOOTER_H, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(Label<PICO_PATH_LEN>::GetLineHeight(FontFn::Small));
    WidgetFunctions::Add(this->status_label);

    // Pop()で戻ってきた場合もここを通るので、履歴の現在位置を開き直す
    this->openCurrent();
}

void MarkdownScene::onExit(){
    //次に戻ってきたとき同じ位置から読み始められるよう、スクロールだけ控えておく。
    //ウィジェット本体の破棄はWidgetFunctions::ClearSceneWidgets()の仕事なので、
    //ここでは生ポインタを捨てるだけ
    this->rememberScroll();

    this->view = nullptr;
    this->back_button = nullptr;
    this->forward_button = nullptr;
    this->exit_button = nullptr;
    this->status_label = nullptr;
}

// ---------- 履歴 ----------

bool MarkdownScene::pushHistory(const char* path){
    FixedString<PICO_STR_L> stored;
    //切り詰まったパスを積むと別の文書を指してしまうので、積まずに失敗を返す
    if(!stored.assign(path)) return false;

    //同じ文書を続けて開いた場合は積み直さない(履歴に同じページが並ぶのを防ぐ)
    if(history_pos >= 0 && history[history_pos].path == stored) return true;

    //前方履歴を捨てる(ブラウザと同じ。戻ってから別のリンクを踏んだ場合)
    history_count = history_pos + 1;

    if(history_count >= kMaxHistory){
        //最も古い1件を押し出す
        for(int i = 0; i < kMaxHistory - 1; i++){
            history[i] = history[i + 1];
        }
        history_count = kMaxHistory - 1;
    }

    history[history_count].path = stored;
    history[history_count].scroll_y = 0;
    history_count++;
    history_pos = history_count - 1;
    return true;
}

void MarkdownScene::rememberScroll(){
    if(!view || history_pos < 0) return;
    history[history_pos].scroll_y = view->getScrollY();
}

bool MarkdownScene::openCurrent(){
    if(!view || history_pos < 0) return false;

    const HistoryEntry& entry = history[history_pos];

    if(!view->load(entry.path.c_str())){
        LOG_SYS_WARN("Markdown: 読み込みに失敗しました (%s)", entry.path.c_str());
        this->showStatus("開けませんでした", PICO_RED);
        return false;
    }

    //load()はスクロールを先頭へ戻すので、控えてあった位置はその後で復元する
    view->setScrollY(entry.scroll_y);

    this->refreshChrome();
    return true;
}

void MarkdownScene::goBack(){
    if(!canGoBack()) return;
    this->rememberScroll();
    history_pos--;
    this->openCurrent();
}

void MarkdownScene::goForward(){
    if(!canGoForward()) return;
    this->rememberScroll();
    history_pos++;
    this->openCurrent();
}

// ---------- リンク ----------

void MarkdownScene::onLinkTap(const FixedString<PICO_PATH_LEN>& url){
    const char* ref = url.c_str();

    //外部リンクはHTTPクライアントが入るまで開けない(PROTOCOL.md参照)
    if(strncmp(ref, "http://", 7) == 0 || strncmp(ref, "https://", 8) == 0){
        this->showStatus("外部リンクはまだ開けません", PICO_RED);
        return;
    }

    //今見ている文書の位置を基準に解決する
    const char* base = (history_pos >= 0) ? history[history_pos].path.c_str() : "/";

    FixedString<PICO_PATH_LEN> resolved;
    if(!PICO_IO::resolve(resolved, base, ref)){
        this->showStatus("パスを解決できません", PICO_RED);
        return;
    }

    //存在しないリンクで履歴を汚さないよう、積む前に確かめる
    if(!OSData::SD.exists(resolved.c_str())){
        this->showStatus("見つかりません", PICO_RED);
        return;
    }

    //進む前に今の位置を控える(戻ってきたときに復元される)
    this->rememberScroll();

    if(!this->pushHistory(resolved.c_str())){
        this->showStatus("パスが長すぎます", PICO_RED);
        return;
    }

    this->openCurrent();
}

// ---------- 見た目の更新 ----------

void MarkdownScene::refreshChrome(){
    //辿れない方向のボタンは灰色にする(押しても無視される)
    if(back_button){
        back_button->setTextColor(canGoBack() ? PICO_BLACK : PICO_LIGHTGREY);
    }
    if(forward_button){
        forward_button->setTextColor(canGoForward() ? PICO_BLACK : PICO_LIGHTGREY);
    }

    if(status_label && history_pos >= 0){
        status_label->setTextColor(PICO_DARKGREY);
        status_label->setText(history[history_pos].path.c_str());
    }
}

void MarkdownScene::showStatus(const char* message, int8_t color){
    if(!status_label) return;
    status_label->setTextColor(color);
    status_label->setText(message);
}
