#include "apps/mail/MailScene.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "services/Power_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "hal/Network_Functions.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// 読む画面のヘッダーの行数(差出人 / 日時 / 宛先 / CC・添付)
static constexpr int kHeaderLines = 4;
// NTP同期前(1970年付近)は「今日」が分からない
static constexpr int kMinValidYear = 2020;

void MailScene::FormatListDate(int64_t epoch, char* out, size_t cap){
    if(cap == 0) return;
    out[0] = '\0';
    if(epoch <= 0) return;
    const time_t t = (time_t)epoch;
    const time_t now = time(nullptr);
    struct tm m, n;
    localtime_r(&t, &m);
    localtime_r(&now, &n);
    const bool clock_ok = n.tm_year + 1900 >= kMinValidYear;
    if(clock_ok && m.tm_year == n.tm_year && m.tm_yday == n.tm_yday){
        snprintf(out, cap, "%02d:%02d", m.tm_hour, m.tm_min);
    }else if(!clock_ok || m.tm_year == n.tm_year){
        snprintf(out, cap, "%d/%d", m.tm_mon + 1, m.tm_mday);
    }else{
        snprintf(out, cap, "%d/%d/%d", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday);
    }
}

bool MailScene::ParseHostPort(const char* text, FixedString<PICO_STR_M>& host, uint16_t& port){
    if(!text) return false;
    while(*text == ' ') text++;
    //"imaps://" 等を付けて入れても読む
    const char* scheme = strstr(text, "://");
    if(scheme) text = scheme + 3;
    const char* colon = strchr(text, ':');
    size_t n = colon ? (size_t)(colon - text) : strlen(text);
    while(n > 0 && (text[n - 1] == ' ' || text[n - 1] == '/')) n--;
    if(n == 0) return false;
    for(size_t i = 0; i < n; i++){
        const char c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
        if(!ok) return false;
    }
    if(!host.assign(text, n)) return false;
    port = ImapClient::kDefaultPort;
    if(colon){
        char* end = nullptr;
        const long v = strtol(colon + 1, &end, 10);
        while(end && *end == ' ') end++;
        if(v <= 0 || v > 65535 || (end && *end && *end != '/')) return false;
        port = (uint16_t)v;
    }
    return true;
}

void MailScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int line_h = FontFn::GetFontSize(FontFn::Small) + 2;

    // ---- 上部の行: [戻る] 題名 [設定] [更新] ----
    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setIcon(IconID::ArrowLeft, IconSize::Px16);
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20 + Button::kFrameExtra);
    this->back_button->setOnPressEnd([this](){
        if(this->mode == Mode::Read) this->backToList();
        else SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);
    const int row_h = this->back_button->getLocalRect().h;

    this->refresh_button = new Button(0, y0, "更新");
    this->refresh_button->setIcon(IconID::Refresh, IconSize::Px16);
    this->refresh_button->setFontSize(FontFn::Small);
    this->refresh_button->setH(20 + Button::kFrameExtra);
    this->refresh_button->setX(content.x + content.w - MARGIN - this->refresh_button->getLocalRect().w);
    this->refresh_button->setOnPressEnd([this](){
        //設定を書き換えてから押した場合にも効くよう、読み直してから取り直す
        if(this->client.state() == ImapClient::State::NotConfigured
           || this->client.state() == ImapClient::State::AuthError){
            this->client.loadConfig();
        }
        this->client.refreshNow();
        this->notice.clear();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->refresh_button);

    this->settings_button = new Button(0, y0, "設定");
    this->settings_button->setIcon(IconID::Settings, IconSize::Px16);
    this->settings_button->setFontSize(FontFn::Small);
    this->settings_button->setH(20 + Button::kFrameExtra);
    this->settings_button->setX(this->refresh_button->getLocalRect().x - MARGIN - this->settings_button->getLocalRect().w);
    this->settings_button->setOnPressEnd([this](){ this->openSettings(); });
    WidgetFunctions::Add(this->settings_button);

    const int title_x = this->back_button->getLocalRect().x + this->back_button->getLocalRect().w + MARGIN * 2;
    this->title_label = new Label<PICO_STR_LL>(title_x, y0, "");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxHeight(line_h);
    this->title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    //件名の * や _ をマークアップとして消さない
    this->title_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->title_label);

    // ---- 一番下: 状態の1行 ----
    const int status_y = content.y + content.h - MARGIN - line_h;
    this->status_label = new Label<PICO_STR_LL>(content.x + MARGIN, status_y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(line_h);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    // ---- 一覧 ----
    const int list_y = y0 + row_h + MARGIN;
    this->mail_list = new ScrollList(content.x + MARGIN, list_y, content.w - MARGIN * 2,
                                     status_y - MARGIN - list_y, ImapClient::kMaxMessages);
    this->mail_list->setFontSize(FontFn::Small);
    this->mail_list->setEnableIcon(true);
    this->mail_list->setOnSelectItem([this](int index, bool){ this->openMessage(index); });
    WidgetFunctions::Add(this->mail_list);

    // ---- 読む: ヘッダーと本文 ----
    this->header_label = new Label<PICO_STR_1KiB>(content.x + MARGIN, list_y, "");
    this->header_label->setFontSize(FontFn::Small);
    this->header_label->setMaxWidth(content.w - MARGIN * 2);
    //行の高さはフォントの大きさより少し大きいので、4行目の下が欠けないよう少し足す
    this->header_label->setMaxHeight(line_h * kHeaderLines + 4);
    this->header_label->setDisableAutoTextDecoration(true);
    this->header_label->setTextColor(PICO_DARKGREY);
    WidgetFunctions::Add(this->header_label);

    const int body_y = list_y + line_h * kHeaderLines + 4 + MARGIN;
    this->body_view = new TextView(content.x + MARGIN, body_y, content.w - MARGIN * 2, status_y - MARGIN - body_y);
    WidgetFunctions::Add(this->body_view);

    this->client.loadConfig();

    this->seen_list_rev = this->client.listRevision() - 1;
    this->seen_status_rev = this->client.statusRevision() - 1;
    this->seen_body_rev = this->client.bodyRevision();
    this->list_count = 0;
    this->frames_since_enter = 0;
    this->pending = Pending::None;
    this->shown_yday = -1;

    //上へ別のシーンを Push() して戻ったときは、読んでいたメールを開き直す
    if(this->mode == Mode::Read && this->reading_uid != 0) this->client.openMessage(this->reading_uid);
    this->setMode(this->mode);
    this->refreshList();
    this->refreshStatus();
}

void MailScene::setMode(Mode m){
    this->mode = m;
    const bool list = (m == Mode::List);
    this->refresh_button->setVisible(list);
    this->settings_button->setVisible(list);
    this->mail_list->setVisible(list);
    this->header_label->setVisible(!list);
    this->body_view->setVisible(!list);
    this->back_button->setText(list ? "戻る" : "一覧");

    //題名の幅は右のボタンの有無で変わる
    const Rect content = Scene::contentRect();
    const int title_x = this->back_button->getLocalRect().x + this->back_button->getLocalRect().w + MARGIN * 2;
    const int right = list ? this->settings_button->getLocalRect().x - MARGIN * 2 : content.x + content.w - MARGIN;
    this->title_label->setX(title_x);
    this->title_label->setMaxWidth(right - title_x);

    if(list){
        this->refreshTitle();
    }else{
        this->refreshHeader();
        this->refreshBody();
    }
    this->refreshStatus();
}

void MailScene::refreshTitle(){
    if(!this->title_label) return;
    if(this->mode == Mode::Read){
        const Imap::Summary* s = this->client.findMessage(this->reading_uid);
        const char* subject = (this->client.bodyUid() == this->reading_uid && this->client.bodyOk())
                            ? this->client.bodyInfo().subject() : (s ? s->subject.c_str() : "");
        this->title_label->setTextColor(PICO_BLACK);
        this->title_label->setText(*subject ? subject : "(件名なし)");
        return;
    }
    FixedString<PICO_STR_LL> t("メール");
    if(this->client.loaded()){
        const int unseen = this->client.unseenCount();
        if(unseen > 0) t.appendFormat(" (未読 %d)", unseen);
    }
    this->title_label->setTextColor(PICO_BLACK);
    this->title_label->setText(t);
}

void MailScene::refreshList(){
    if(!this->mail_list) return;
    const time_t now = time(nullptr);
    struct tm n;
    localtime_r(&now, &n);
    this->shown_yday = n.tm_yday;

    //並び(UIDの列)が同じなら、行を作り直さずに中身だけ書き換える(スクロールの位置を保つため)
    const int count = this->client.messageCount();
    bool same = (count == this->list_count) && (this->mail_list->getItemCount() == count);
    for(int i = 0; same && i < count; i++){
        if(this->list_uids[i] != this->client.messageAt(i).uid) same = false;
    }
    if(!same) this->mail_list->clear();

    for(int i = 0; i < count; i++){
        const Imap::Summary& m = this->client.messageAt(i);
        ScrollListTools::Item item;
        item.icon = m.seen ? IconID::MailOpened : IconID::Mail;
        char date[24];
        FormatListDate(m.date, date, sizeof(date));
        if(date[0]){ item.text.append(date); item.text.append(' '); }
        item.text.append(m.from.empty() ? "(差出人なし)" : m.from.c_str());
        item.text.append("  ");
        item.text.append(m.subject.empty() ? "(件名なし)" : m.subject.c_str());
        item.color = m.seen ? PICO_DARKGREY : -1;

        if(same){
            ScrollListTools::Item* cur = this->mail_list->itemAt(i);
            if(cur && (cur->icon != item.icon || cur->color != item.color || !(cur->text == item.text))){
                *cur = item;
                this->mail_list->needsRender();
            }
        }else{
            this->mail_list->add(item);
            this->list_uids[i] = m.uid;
        }
    }
    this->list_count = count;
    if(this->mode == Mode::List) this->refreshTitle();
}

// 宛先の欄を1行に収める形にする: 最初の1人の名前と、ほかにもいれば「他N人」
static void ShortRecipients(const char* decoded, FixedString<PICO_STR_256B>& out){
    char first[PICO_STR_L];
    MailMime::DisplayAddress(decoded, first, sizeof(first));
    out.assign(first);
    int others = 0;
    bool quoted = false;
    int angle = 0;
    for(const char* p = decoded; *p; p++){
        if(*p == '"') quoted = !quoted;
        else if(!quoted && *p == '<') angle++;
        else if(!quoted && *p == '>' && angle > 0) angle--;
        else if(!quoted && angle == 0 && *p == ',') others++;
    }
    if(others > 0) out.appendFormat(" 他%d人", others);
}

void MailScene::refreshHeader(){
    if(!this->header_label) return;
    const bool have = this->client.bodyUid() == this->reading_uid && this->client.bodyOk();
    const Imap::Summary* s = this->client.findMessage(this->reading_uid);
    const MailMime::TextExtractor& info = this->client.bodyInfo();

    FixedString<PICO_STR_1KiB> text;
    //1行に収めるため名前だけにする(名前の無い差出人はアドレスになる)
    FixedString<PICO_STR_256B> who;
    if(have && *info.from()) ShortRecipients(info.from(), who);
    else if(s) who.assign(s->from);
    text.append("差出人: ");
    text.append(who);
    text.append('\n');

    int64_t epoch = s ? s->date : 0;
    if(have && epoch == 0) MailMime::ParseDate(info.date(), epoch);
    if(epoch > 0){
        const time_t t = (time_t)epoch;
        struct tm m;
        localtime_r(&t, &m);
        static const char* const kWeek[] = {"日", "月", "火", "水", "木", "金", "土"};
        text.appendFormat("日時: %d/%02d/%02d(%s) %02d:%02d\n", m.tm_year + 1900, m.tm_mon + 1, m.tm_mday,
                          kWeek[m.tm_wday], m.tm_hour, m.tm_min);
    }else{
        text.append("日時: \n");
    }
    if(have){
        ShortRecipients(info.to(), who);
        text.append("宛先: ");
        text.append(who);
        text.append('\n');
        if(info.attachmentCount() > 0){
            text.appendFormat("添付: %d件(%s)", info.attachmentCount(), info.attachmentNames());
        }else if(*info.cc()){
            ShortRecipients(info.cc(), who);
            text.append("CC: ");
            text.append(who);
        }
    }
    this->header_label->setText(text);
}

void MailScene::refreshBody(){
    if(!this->body_view) return;
    if(this->client.bodyUid() == this->reading_uid && !this->client.bodyLoading()){
        if(this->client.bodyOk()){
            if(this->client.bodyLength() > 0){
                this->body_view->setDocument(this->client.bodyText(), (int)this->client.bodyLength());
            }else{
                this->body_message.assign("(本文がありません)");
                this->body_view->setDocument(this->body_message.c_str(), (int)this->body_message.length());
            }
        }else{
            this->body_message.assign("読めませんでした: ");
            this->body_message.append(this->client.bodyError());
            this->body_view->setDocument(this->body_message.c_str(), (int)this->body_message.length());
        }
    }else{
        this->body_message.assign("読み込み中…");
        this->body_view->setDocument(this->body_message.c_str(), (int)this->body_message.length());
    }
    this->body_view->scrollToTop();
}

void MailScene::setNotice(const char* text, int8_t color){
    this->notice.assign(text ? text : "");
    this->notice_color = color;
    this->refreshStatus();
}

void MailScene::refreshStatus(){
    if(!this->status_label) return;
    using S = ImapClient::State;
    const S s = this->client.state();
    const bool bad = (s == S::Error || s == S::AuthError || s == S::NotConfigured || s == S::Offline);

    const char* text = this->client.statusText();
    int8_t color = bad ? PICO_RED : PICO_DARKGREY;
    FixedString<PICO_STR_LL> buf;
    if(!*text && !this->notice.empty()){
        text = this->notice.c_str();
        color = this->notice_color;
    }else if(!*text && this->mode == Mode::Read){
        const bool have = this->client.bodyUid() == this->reading_uid && this->client.bodyOk();
        if(have && this->client.bodyTruncated()) text = "長いので途中までを表示";
        else if(have && this->client.bodyFromHtml()) text = "HTMLを文字だけにして表示";
    }else if(!*text && !this->client.loaded()){
        text = "読み込み中…";
    }else if(!*text && this->client.loaded()){
        if(this->client.messageCount() == 0){
            text = "メールはありません";
        }else if(this->client.totalCount() > (uint32_t)this->client.messageCount()){
            buf.appendFormat("新しい %d 通を表示(全 %u 通)", this->client.messageCount(),
                             (unsigned)this->client.totalCount());
            text = buf.c_str();
        }
    }
    this->status_label->setTextColor(color);
    this->status_label->setText(text);
}

void MailScene::openMessage(int row){
    if(row < 0 || row >= this->list_count || row >= this->client.messageCount()) return;
    const Imap::Summary& m = this->client.messageAt(row);
    this->reading_uid = m.uid;
    this->notice.clear();
    this->client.openMessage(m.uid);
    this->setMode(Mode::Read);
}

void MailScene::backToList(){
    this->client.closeMessage();
    this->notice.clear();
    //TextView は本文の置き場を指しているので、次の本文で書き換わる前に外しておく
    this->body_message.clear();
    this->body_view->setDocument(this->body_message.c_str(), 0);
    this->setMode(Mode::List);
    this->refreshList();
}

void MailScene::openSettings(){
    //今のサーバを初期値にする(パスワードは平文で持たないので空欄から始まる)
    const ImapClient::Config& c = this->client.config();
    FixedString<PICO_STR_L> current;
    if(!c.host.empty()){
        current.assign(c.host);
        if(c.port != ImapClient::kDefaultPort) current.appendFormat(":%u", (unsigned)c.port);
    }
    auto* dialog = new InputDialog("IMAPサーバ(例: imap.gmail.com。ポートが993以外なら :番号):", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(current.c_str());
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            if(ParseHostPort(dialog->getInput().c_str(), this->input_host, this->input_port)){
                this->pending = Pending::User;
            }else{
                this->setNotice("サーバの名前が読めません(例: imap.example.com:993)", PICO_RED);
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void MailScene::openUserDialog(){
    auto* dialog = new InputDialog("ユーザー名(ふつうはメールアドレス):", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(this->client.config().user.c_str());
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit && !dialog->getInput().empty()){
            this->input_user.assign(dialog->getInput().c_str());
            this->pending = Pending::Password;
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void MailScene::openPasswordDialog(){
    //Gmail等は普段のパスワードでは入れない(アプリパスワードを作る)ので、ここで一言添える
    const bool keep = !this->client.config().password.empty() && this->client.config().user == this->input_user;
    auto* dialog = new InputDialog(keep ? "パスワード(Gmailはアプリパスワード。空欄で変更なし):"
                                        : "パスワード(Gmailはアプリパスワード):", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog, keep](bool is_submit){
        if(is_submit){
            const FixedString<PICO_STR_LL> pw = dialog->getInput();
            if(pw.empty() && !keep){
                this->setNotice("パスワードが空なので保存しませんでした", PICO_RED);
            }else if(ImapClient::SaveAccount(this->input_host.c_str(), this->input_port,
                                             this->input_user.c_str(), pw.c_str())){
                this->client.loadConfig();
                this->client.refreshNow();
                this->list_count = 0;
                this->mail_list->clear();
                this->setNotice("アカウントを保存しました");
            }else{
                this->setNotice("アカウントを保存できませんでした", PICO_RED);
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void MailScene::scrollBody(int dy){
    if(this->mode != Mode::Read || !this->body_view) return;
    int y = this->body_view->getScrollY() + dy;
    if(y < 0) y = 0;
    this->body_view->setScrollY(y);
}

bool MailScene::onKey(const KeyInputFunctions::Event& ev){
    using K = KeyInputFunctions::Key;
    const int line_h = FontFn::GetFontSize(FontFn::Small) + 2;
    const int page = this->body_view ? this->body_view->getLocalRect().h - line_h : 100;
    switch(ev.key){
        case K::Escape:
        case K::Backspace:
            if(this->mode == Mode::Read) this->backToList();
            else SceneFunctions::Pop();
            return true;
        case K::Up:       if(this->mode == Mode::Read){ this->scrollBody(-line_h * 2); return true; } return false;
        case K::Down:     if(this->mode == Mode::Read){ this->scrollBody(line_h * 2); return true; } return false;
        case K::PageUp:   this->scrollBody(-page); return this->mode == Mode::Read;
        case K::PageDown: this->scrollBody(page); return this->mode == Mode::Read;
        case K::Home:     if(this->mode == Mode::Read){ this->body_view->scrollToTop(); return true; } return false;
        default:
            break;
    }
    if(ev.isPlainChar() && ev.cp == ' ' && this->mode == Mode::Read){
        this->scrollBody(page);
        return true;
    }
    if(ev.isPlainChar() && ev.cp == 'r' && this->mode == Mode::List){
        this->client.refreshNow();
        return true;
    }
    return false;
}

void MailScene::onUpdate(){
    //通信中はスリープさせない(応答を待っている間に止まらないように)
    if(this->client.busy()) PowerFunctions::KeepAwake();
    //1回描いてから繋ぎに行く(最初の接続はTLSのハンドシェイクで1〜2秒止まるため)
    if(this->frames_since_enter < 2){
        this->frames_since_enter++;
        return;
    }

    if(this->pending == Pending::User){
        this->pending = Pending::None;
        this->openUserDialog();
    }else if(this->pending == Pending::Password){
        this->pending = Pending::None;
        this->openPasswordDialog();
    }

    this->client.update(NetworkFunctions::IsConnected());

    if(this->client.listRevision() != this->seen_list_rev){
        this->seen_list_rev = this->client.listRevision();
        this->refreshList();
        this->refreshStatus();
    }
    if(this->client.bodyRevision() != this->seen_body_rev){
        this->seen_body_rev = this->client.bodyRevision();
        if(this->mode == Mode::Read && this->client.bodyUid() == this->reading_uid){
            this->refreshTitle();
            this->refreshHeader();
            this->refreshBody();
            this->refreshStatus();
        }
    }
    if(this->client.statusRevision() != this->seen_status_rev){
        this->seen_status_rev = this->client.statusRevision();
        this->refreshStatus();
    }

    //日付が変わると一覧の日付の出し方(今日なら時刻)が変わる
    const unsigned long now_ms = millis();
    if(now_ms - this->last_clock_check_ms >= 10000){
        this->last_clock_check_ms = now_ms;
        const time_t now = time(nullptr);
        struct tm n;
        localtime_r(&now, &n);
        if(n.tm_yday != this->shown_yday){
            this->list_count = 0; //日付の文字が変わるので行を作り直す
            this->refreshList();
        }
    }
}

void MailScene::onExit(){
    //onUpdate() が来なくなるので、通信を打ち切って接続(TLSの約40KB)も返す
    this->client.stop();
    this->pending = Pending::None;

    this->back_button = nullptr;
    this->title_label = nullptr;
    this->status_label = nullptr;
    this->refresh_button = nullptr;
    this->settings_button = nullptr;
    this->mail_list = nullptr;
    this->header_label = nullptr;
    this->body_view = nullptr;
}
