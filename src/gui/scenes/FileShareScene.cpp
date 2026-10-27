#include "gui/scenes/FileShareScene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/QRCodeView.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "services/FileShare_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "hal/Network_Functions.hpp"

#include <Arduino.h>
#include <cstdio>

namespace {
    void FormatSize(uint64_t n, char* out, size_t cap){
        if(n < 1024) snprintf(out, cap, "%uB", (unsigned)n);
        else if(n < 1024u * 1024u) snprintf(out, cap, "%.1fKB", (double)n / 1024.0);
        else snprintf(out, cap, "%.1fMB", (double)n / (1024.0 * 1024.0));
    }

    bool Connected(){
#if defined(PICOOS_PC)
        return true; // PCビルドは 127.0.0.1 で開ける
#else
        return NetworkFunctions::IsConnected();
#endif
    }
}

void FileShareScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int w = content.w - MARGIN * 2;

    Button* back = new Button(content.x + MARGIN, y0, "戻る");
    back->setIcon(IconID::ArrowLeft, IconSize::Px16);
    back->setFontSize(FontFn::Small);
    back->setH(20 + Button::kFrameExtra);
    back->setOnPressEnd([](){ SceneFunctions::Pop(); }); // 待ち受けをやめるのは onExit()
    WidgetFunctions::Add(back);
    const Rect back_box = back->getLocalRect();

    auto* title = new Label<PICO_STR_M>(back_box.x + back_box.w + MARGIN, y0, kAppName);
    title->setFontSize(FontFn::Small);
    title->setY(y0 + (back_box.h - FontFn::GetFontSize(FontFn::Small)) / 2);
    title->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(title);

    const int small_h = Label<PICO_STR_LL>::GetLineHeight(FontFn::Small);
    const int big_h = Label<PICO_STR_M>::GetLineHeight(FontFn::Normal);
    int y = y0 + back_box.h + MARGIN * 2;

    FixedString<PICO_STR_LL> err;
    this->failed = !FileShareFunctions::Start(err);
    if(this->failed){
        char msg[PICO_STR_256B];
        snprintf(msg, sizeof(msg), "始められませんでした: %s", err.c_str());
        auto* l = new Label<PICO_STR_256B>(content.x + MARGIN, y, msg);
        l->setFontSize(FontFn::Small);
        l->setMaxWidth(w);
        l->setMaxHeight(small_h * 4);
        l->setDisableAutoTextDecoration(true);
        WidgetFunctions::Add(l);
        return;
    }

    // 右にキー付きのURLのQR、左にキー(打ち写す人向け)。URLは長いのでQRの下へ横いっぱいに
    this->qr = new QRCodeView(content.x + content.w - kQrSize, y, kQrSize);
    WidgetFunctions::Add(this->qr);
    const int text_w = content.w - kQrSize - MARGIN;
    const int row_top = y;

    auto addSmall = [&](const char* text, int lines, int width, int8_t color) -> Label<PICO_STR_LL>* {
        auto* l = new Label<PICO_STR_LL>(content.x + MARGIN, y, text);
        l->setFontSize(FontFn::Small);
        l->setMaxWidth(width);
        l->setMaxHeight(small_h * lines);
        l->setTextColor(color);
        l->setDisableAutoTextDecoration(true);
        WidgetFunctions::Add(l);
        y += small_h * lines;
        return l;
    };

    addSmall("ブラウザで開いてキーを入れる", 2, text_w, PICO_FORECOLOR);
    this->key_label = new Label<PICO_STR_M>(content.x + MARGIN, y, FileShareFunctions::Key());
    this->key_label->setFontSize(FontFn::Normal);
    this->key_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->key_label);
    y += big_h + 2;
    addSmall("QRならキーは不要", 1, text_w, PICO_DARKGREY);
    if(y < row_top + kQrSize) y = row_top + kQrSize;
    y += 2;

    this->url_ip_label = addSmall("", 1, w, PICO_FORECOLOR);
    this->url_name_label = addSmall("", 1, w, PICO_DARKGREY);
    y += MARGIN;

    // 今の転送は割合を先に出す(長い名前は後ろが切れる)
    this->current_label = addSmall("", 1, w, PICO_BLUE);
    y += 2;

    // 注意書きは下へ寄せ、最近の操作はその間に入るだけ
    auto* note = new Label<PICO_STR_256B>(content.x + MARGIN, y,
        "暗号化しない送り方です。信頼できるWi-Fiで使ってください");
    note->setFontSize(FontFn::Small);
    note->setMaxWidth(w);
    note->setTextColor(PICO_DARKGREY);
    note->setDisableAutoTextDecoration(true);
    const int note_y = content.y + content.h - MARGIN - note->getH();
    note->setY(note_y);
    WidgetFunctions::Add(note);

    int lines = (note_y - y) / small_h;
    if(lines > FileShareFunctions::kRecentCount) lines = FileShareFunctions::kRecentCount;
    if(lines < 1) lines = 1;
    this->recent_lines = lines;
    this->recent_label = new Label<PICO_STR_1KiB>(content.x + MARGIN, y, "");
    this->recent_label->setFontSize(FontFn::Small);
    this->recent_label->setMaxWidth(w);
    this->recent_label->setMaxHeight(small_h * lines);
    this->recent_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->recent_label);

    this->shown_connected = -1;
    this->shown_revision = 0xFFFFFFFFu;
    this->shown_done = UINT64_MAX;
    this->refreshAddress(true);
    this->refreshActivity(true);
}

void FileShareScene::refreshAddress(bool force){
    const int8_t connected = Connected() ? 1 : 0;
    if(!force && connected == this->shown_connected) return;
    this->shown_connected = connected;
    FixedString<PICO_STR_L> by_name, by_ip;
    FileShareFunctions::PageUrls(by_name, by_ip);
    if(connected){
        this->url_ip_label->setTextColor(PICO_FORECOLOR);
        this->url_ip_label->setText(by_ip.empty() ? by_name.c_str() : by_ip.c_str());
        this->url_name_label->setText(by_ip.empty() ? "" : by_name.c_str());
        FixedString<PICO_STR_LL> url;
        FileShareFunctions::KeyUrl(url);
        this->qr->setText(url.c_str(), url.length());
    }else{
        this->url_ip_label->setTextColor(PICO_RED);
        this->url_ip_label->setText("Wi-Fiに繋がっていません。設定のWi-Fiタブで繋いでください");
        this->url_name_label->setText("");
        this->qr->setText("");
    }
}

void FileShareScene::refreshActivity(bool force){
    const auto& cur = FileShareFunctions::Current();
    const uint32_t rev = FileShareFunctions::Revision();
    const uint32_t now = millis();
    const bool moving = cur.op != FileShareFunctions::Op::None && cur.done != this->shown_done;
    if(!force && rev == this->shown_revision && !(moving && now - this->last_refresh_ms >= kRefreshMs)) return;
    this->last_refresh_ms = now;

    if(rev != this->shown_revision || force){
        this->shown_revision = rev;
        FixedString<PICO_STR_1KiB> text;
        const int n = FileShareFunctions::RecentCount();
        for(int i = 0; i < n && i < this->recent_lines; i++){
            if(i > 0) text.append("\n");
            text.append(FileShareFunctions::Recent(i));
        }
        this->recent_label->setText(n > 0 ? text.c_str() : "まだ何も送っていません");
    }

    this->shown_done = cur.done;
    if(cur.op == FileShareFunctions::Op::None){
        this->current_label->setText("");
        return;
    }
    char done[16], total[16], line[PICO_STR_LL];
    FormatSize(cur.done, done, sizeof(done));
    FormatSize(cur.total, total, sizeof(total));
    const unsigned pct = cur.total > 0 ? (unsigned)(cur.done * 100 / cur.total) : 100;
    snprintf(line, sizeof(line), "%s %u%% %s/%s %s",
             cur.op == FileShareFunctions::Op::Upload ? "↑" : "↓", pct, done, total, cur.name.c_str());
    this->current_label->setText(line);
}

void FileShareScene::onUpdate(){
    if(this->failed) return;
    PowerFunctions::KeepAwake(); // 待ち受けの間は眠らない(眠るとWi-Fiが省電力になり、遅くなる)
    FileShareFunctions::Update();
    this->refreshAddress(false);
    this->refreshActivity(false);
}

void FileShareScene::onExit(){
    FileShareFunctions::Stop();
    this->url_ip_label = nullptr;
    this->url_name_label = nullptr;
    this->key_label = nullptr;
    this->current_label = nullptr;
    this->recent_label = nullptr;
    this->qr = nullptr;
}
