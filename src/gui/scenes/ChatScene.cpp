#include "gui/scenes/ChatScene.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>

static_assert(ChatLogView::kMaxEntries >= ChatClient::kMaxMessages,
              "ChatLogView が ChatClient の発言を全部並べられない");

// 入力欄の高さ(16pxの文字 + 枠と余白)
static constexpr int kInputH = 22;
// 一覧の下のボタンの行の高さ(文字の周りの余白を付けずに、この高さで描く)
static constexpr int kActionH = 24;

void ChatScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    // ---- 上部の行: [戻る] タイトル [招待] [更新] ----
    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([this](){
        if(this->mode != Mode::List) this->backToList();
        else SceneFunctions::Pop();
    });
    WidgetFunctions::Add(this->back_button);
    this->row_h = this->back_button->getLocalRect().h;

    this->refresh_button = new Button(0, y0, "更新");
    this->refresh_button->setFontSize(FontFn::Small);
    this->refresh_button->setH(20);
    this->refresh_button->setX(content.x + content.w - MARGIN - this->refresh_button->getLocalRect().w);
    this->refresh_button->setOnPressEnd([this](){
        //設定を書き換えてから押した場合にも効くよう、読み直してから取り直す
        if(this->client.state() == ChatClient::State::NotConfigured
           || this->client.state() == ChatClient::State::AuthError){
            this->client.loadConfig();
        }
        if(this->mode == Mode::Search) this->startSearch();
        this->client.refreshNow();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->refresh_button);

    //プライベートチャットの中でだけ出す
    this->invite_button = new Button(0, y0, "招待");
    this->invite_button->setFontSize(FontFn::Small);
    this->invite_button->setH(20);
    this->invite_button->setX(this->refresh_button->getLocalRect().x - MARGIN - this->invite_button->getLocalRect().w);
    this->invite_button->setOnPressEnd([this](){
        if(this->client.requestInvite()) this->setNotice("参加コードを発行しています…");
        else if(this->client.actionBusy()) this->setNotice("前の操作が終わるまで待ってください", PICO_RED);
    });
    WidgetFunctions::Add(this->invite_button);

    //タイトルは [招待] がある場合の幅に合わせる(部屋ごとに位置を変えない)
    const int title_x = this->back_button->getLocalRect().x + this->back_button->getLocalRect().w + MARGIN * 2;
    const int title_w = this->invite_button->getLocalRect().x - MARGIN * 2 - title_x;
    this->title_label = new Label<PICO_STR_M>(title_x, y0, "");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxWidth(title_w);
    this->title_label->setMaxHeight(FontFn::GetFontSize(FontFn::Small) + 2);
    this->title_label->setY(y0 + (this->row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->title_label);

    this->body_top = y0 + this->row_h + MARGIN;
    this->input_row_y = content.y + content.h - MARGIN - kInputH;
    this->action_row_y = content.y + content.h - MARGIN - kActionH;

    // ---- 一覧の下の行: [部屋を探す] [コードで参加] ----
    const int half_w = (content.w - MARGIN * 3) / 2;
    this->search_button = new Button(content.x + MARGIN, this->action_row_y, "部屋を探す");
    this->search_button->setFontSize(FontFn::Small);
    this->search_button->setAllowTextSpacing(false);
    this->search_button->setH(kActionH);
    this->search_button->setW(half_w);
    this->search_button->setOnPressEnd([this](){ this->openSearchInput(); });
    WidgetFunctions::Add(this->search_button);

    this->code_button = new Button(content.x + MARGIN * 2 + half_w, this->action_row_y, "コードで参加");
    this->code_button->setFontSize(FontFn::Small);
    this->code_button->setAllowTextSpacing(false);
    this->code_button->setH(kActionH);
    this->code_button->setW(half_w);
    this->code_button->setOnPressEnd([this](){ this->openCodeInput(); });
    WidgetFunctions::Add(this->code_button);

    // ---- 部屋の中: 入力欄 + [送信] ----
    this->send_button = new Button(0, this->input_row_y, "送信中");
    this->send_button->setFontSize(FontFn::Small);
    this->send_button->setH(kInputH);
    //押すと文字が変わる(送信/送信中)ので、長いほうで幅を固定する
    this->send_button->setW(this->send_button->getLocalRect().w);
    this->send_button->setX(content.x + content.w - MARGIN - this->send_button->getLocalRect().w);
    this->send_button->setOnPressEnd([this](){ this->sendDraft(); });
    WidgetFunctions::Add(this->send_button);

    const int input_w = this->send_button->getLocalRect().x - MARGIN - (content.x + MARGIN);
    this->input = new Textbox<PICO_STR_LL>(this->draft.c_str(), content.x + MARGIN, this->input_row_y,
                                           input_w, kInputH, true);
    this->input->setFontSize(FontFn::Small);
    this->input->setPlaceholder("メッセージ");
    this->input->setDefaultHeight(kInputH);
    WidgetFunctions::Add(this->input);

    // ---- 状態の1行(失敗の理由など)と、本体(一覧/発言) ----
    //配置は applyMode() が決める(一覧では2行、部屋の中では1行)
    this->status_label = new Label<PICO_STR_LL>(content.x + MARGIN, 0, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    WidgetFunctions::Add(this->status_label);

    this->room_list = new ScrollList(content.x + MARGIN, this->body_top, content.w - MARGIN * 2, 10,
                                     kMaxListRows);
    this->room_list->setFontSize(FontFn::Small);
    this->room_list->setEnableIcon(true);
    //部屋は1回のタップで開く(選ぶだけの操作が無いので)
    this->room_list->setOnSelectItem([this](int index, bool){ this->onListTap(index); });
    WidgetFunctions::Add(this->room_list);

    this->log_view = new ChatLogView(content.x + MARGIN, this->body_top, content.w - MARGIN * 2, 10);
    this->log_view->setSource(&this->client);
    WidgetFunctions::Add(this->log_view);

    this->client.loadConfig();
    //前回の部屋を開いたまま戻ってきた(Push()から戻った)なら、部屋の中から始める
    if(this->mode == Mode::Room && this->client.room() == 0) this->mode = Mode::List;

    this->seen_rooms_rev = this->client.roomsRevision() - 1;
    this->seen_msgs_rev = this->client.messagesRevision() - 1;
    this->seen_status_rev = this->client.statusRevision() - 1;
    this->seen_action_rev = this->client.actionRevision();
    this->seen_room_lost_rev = this->client.roomLostRevision();
    this->seen_sending = !this->client.sending();
    this->frames_since_enter = 0;

    this->applyMode();
}

void ChatScene::applyMode(){
    const bool in_room = (this->mode == Mode::Room);
    const bool searching = (this->mode == Mode::Search);
    const int line_h = FontFn::GetFontSize(FontFn::Small) + 2;

    this->room_list->setVisible(!in_room);
    this->log_view->setVisible(in_room);
    this->input->setVisible(in_room);
    this->send_button->setVisible(in_room);
    this->search_button->setVisible(!in_room);
    this->code_button->setVisible(this->mode == Mode::List);
    this->search_button->setText(searching ? "別の語で探す" : "部屋を探す");

    const ChatProto::Room* r = in_room ? this->client.findRoom(this->client.room()) : nullptr;
    this->invite_button->setVisible(r && r->is_private);

    //状態の行は、部屋の中では入力欄の上に1行、一覧/検索ではボタンの行の上に2行(設定の案内が長いので)
    const int status_lines = in_room ? 1 : 2;
    const int bottom = (in_room ? this->input_row_y : this->action_row_y) - MARGIN;
    const int status_y = bottom - line_h * status_lines;
    this->status_label->setY(status_y);
    this->status_label->setMaxHeight(line_h * status_lines);

    const int body_h = status_y - MARGIN - this->body_top;
    if(in_room){
        this->log_view->setH(body_h);
        this->log_view->refresh();
        this->log_view->scrollToBottom();
    }else{
        this->room_list->setH(body_h);
        this->refreshRoomList();
    }

    this->refreshTitle();
    this->refreshPlaceholder();
    this->refreshStatus();
    this->refreshSendButton();
}

void ChatScene::refreshTitle(){
    if(!this->title_label) return;
    if(this->mode == Mode::Room){
        const ChatProto::Room* r = this->client.findRoom(this->client.room());
        FixedString<PICO_STR_M> t(r && r->is_private ? "" : "# ");
        t.append(r ? r->name.c_str() : "");
        this->title_label->setText(t);
    }else if(this->mode == Mode::Search){
        this->title_label->setText("部屋を探す");
    }else{
        this->title_label->setText("チャット");
    }
}

void ChatScene::refreshRoomList(){
    if(!this->room_list) return;
    this->room_list->clear();
    this->list_count = 0;

    if(this->mode == Mode::Search){
        for(int i = 0; i < this->client.searchCount() && this->list_count < kMaxListRows; i++){
            const ChatProto::SearchHit& h = this->client.searchAt(i);
            ScrollListTools::Item item;
            item.icon = IconID::Messages;
            item.text.append(h.name);
            item.text.appendFormat(" (%lu人)", (unsigned long)h.members);
            //参加済みは灰色(タップすると開くだけ)
            if(h.joined) item.color = PICO_DARKGREY;
            this->room_list->add(item);
            this->list_room_ids[this->list_count++] = h.id;
        }
        return;
    }

    for(int i = 0; i < this->client.roomCount() && this->list_count < kMaxListRows; i++){
        const ChatProto::Room& r = this->client.roomAt(i);
        ScrollListTools::Item item;
        item.icon = r.is_private ? IconID::Lock : IconID::Messages;
        item.text.append(r.name);
        if(r.unread > 0){
            item.text.appendFormat("  (%lu)", (unsigned long)r.unread);
            item.color = PICO_RED;
        }
        this->room_list->add(item);
        this->list_room_ids[this->list_count++] = r.id;
    }
}

void ChatScene::setNotice(const char* text, int8_t color){
    this->notice.assign(text ? text : "");
    this->notice_color = color;
    this->refreshStatus();
}

void ChatScene::refreshStatus(){
    if(!this->status_label) return;
    const ChatClient::State s = this->client.state();
    const bool bad = (s == ChatClient::State::Error || s == ChatClient::State::AuthError
                   || s == ChatClient::State::NotConfigured || s == ChatClient::State::Offline);

    const char* text = this->client.statusText();
    int8_t color = bad ? PICO_RED : PICO_DARKGREY;
    if(!*text && !this->notice.empty()){
        text = this->notice.c_str();
        color = this->notice_color;
    }else if(!bad && this->mode == Mode::List && this->client.roomCount() == 0 && this->client.roomsRevision() != 0){
        text = "参加している部屋がありません。下のボタンから探すか、参加コードで入れます";
    }
    this->status_label->setTextColor(color);
    this->status_label->setText(text);
}

void ChatScene::refreshPlaceholder(){
    if(!this->log_view) return;
    if(this->client.messagesLoaded()) this->log_view->setPlaceholder("まだ発言がありません");
    else this->log_view->setPlaceholder("読み込み中…");
}

void ChatScene::refreshSendButton(){
    if(!this->send_button) return;
    const bool sending = this->client.sending();
    this->seen_sending = sending;
    this->send_button->setText(sending ? "送信中" : "送信");
}

void ChatScene::openRoom(uint32_t room_id){
    if(room_id == 0) return;
    this->client.setRoom(room_id);
    this->mode = Mode::Room;
    this->notice.clear();
    this->applyMode();
}

void ChatScene::backToList(){
    KeyboardFunctions::HideAll();
    this->client.setRoom(0);
    this->mode = Mode::List;
    this->notice.clear();
    this->applyMode();
}

void ChatScene::onListTap(int index){
    if(index < 0 || index >= this->list_count) return;
    const uint32_t id = this->list_room_ids[index];
    if(this->mode != Mode::Search){
        this->openRoom(id);
        return;
    }
    //検索結果: 参加済みなら開くだけ、まだなら参加してから開く(onActionDone)
    if(this->client.findRoom(id)){
        this->openRoom(id);
        return;
    }
    if(this->client.joinOpen(id)) this->setNotice("参加しています…");
    else this->setNotice("前の操作が終わるまで待ってください", PICO_RED);
}

void ChatScene::openSearchInput(){
    auto* dialog = new InputDialog("探す部屋の名前:", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setInput(this->search_query.c_str()); //前回の語から直せるようにしておく
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            this->search_query.assign(dialog->getInput().c_str());
            this->mode = Mode::Search;
            this->applyMode();
            this->startSearch();
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void ChatScene::startSearch(){
    if(this->client.search(this->search_query.c_str())) this->setNotice("探しています…");
    else this->setNotice("前の操作が終わるまで待ってください", PICO_RED);
}

void ChatScene::openCodeInput(){
    auto* dialog = new InputDialog("参加コード:", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            const FixedString<PICO_STR_LL> code = dialog->getInput();
            if(code.empty()){
                //何もしない
            }else if(this->client.joinByCode(code.c_str())){
                this->setNotice("参加しています…");
            }else{
                this->setNotice(this->client.actionBusy() ? "前の操作が終わるまで待ってください"
                                                          : "参加コードが長すぎます", PICO_RED);
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void ChatScene::showInvite(){
    //期限は時計が合っていれば時刻で、合っていなければ「30分」とだけ出す(サーバの時刻が基準)
    char text[96];
    const time_t expires = (time_t)this->client.inviteExpires();
    const time_t now = time(nullptr);
    struct tm tm_local;
    if(now > 1577836800 && localtime_r(&expires, &tm_local)){
        snprintf(text, sizeof(text), "参加コード\n%s\n%02d:%02dまで有効", this->client.inviteCode(),
                 tm_local.tm_hour, tm_local.tm_min);
    }else{
        snprintf(text, sizeof(text), "参加コード\n%s\n30分で無効", this->client.inviteCode());
    }
    LOG_APP_MSG("チャット: 参加コードを発行しました(%s)", this->client.inviteCode());

    auto* dialog = new MsgDialog(text, "閉じる", "OK");
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([dialog](bool){ WidgetFunctions::DestroyLater(dialog); });
}

void ChatScene::onActionDone(){
    using Action = ChatClient::Action;
    const Action a = this->client.lastAction();
    if(!this->client.lastActionOk()){
        this->setNotice(this->client.actionMessage(), PICO_RED);
        return;
    }
    switch(a){
        case Action::Search:
            if(this->mode == Mode::Search) this->refreshRoomList();
            this->setNotice(this->client.searchCount() == 0 ? "見つかりませんでした" : "");
            break;
        case Action::JoinOpen:
        case Action::JoinCode:
            this->openRoom(this->client.joinedRoomId());
            break;
        case Action::Invite:
            //状態の行を消してから、次のフレームでダイアログを出す(同じフレームで半透明のダイアログを
            //重ねると、その下の書き換えが描かれないまま残るため。MarkdownScene の Pending と同じ理由)
            this->setNotice("");
            this->pending_invite_dialog = true;
            break;
        default:
            break;
    }
}

void ChatScene::sendDraft(){
    if(!this->input) return;
    const FixedString<PICO_STR_LL>* text = this->input->getText();
    if(!text || text->empty()) return;

    if(!this->client.send(text->c_str())){
        this->status_label->setTextColor(PICO_RED);
        this->status_label->setText(this->client.sending() ? "前の発言を送っています" : "送れません");
        return;
    }
    this->input->setText("");
    this->draft.clear();
    this->log_view->scrollToBottom();
    this->refreshSendButton();
}

void ChatScene::onUpdate(){
    //1回描いてから繋ぎに行く(最初の接続はTLSのハンドシェイクで1〜2秒止まるため)
    if(this->frames_since_enter < 2){
        this->frames_since_enter++;
        return;
    }

    if(this->pending_invite_dialog){
        this->pending_invite_dialog = false;
        this->showInvite();
    }

    this->client.update(NetworkFunctions::IsConnected());

    if(this->client.roomLostRevision() != this->seen_room_lost_rev){
        this->seen_room_lost_rev = this->client.roomLostRevision();
        //追い出された/部屋が消えた。理由(サーバの1行目)を残して一覧へ戻す
        FixedString<PICO_STR_LL> why(this->client.statusText());
        if(this->mode == Mode::Room){
            KeyboardFunctions::HideAll();
            this->mode = Mode::List;
            this->applyMode();
        }
        this->setNotice(why.c_str(), PICO_RED);
    }
    if(this->client.actionRevision() != this->seen_action_rev){
        this->seen_action_rev = this->client.actionRevision();
        this->onActionDone();
    }
    if(this->client.roomsRevision() != this->seen_rooms_rev){
        this->seen_rooms_rev = this->client.roomsRevision();
        if(this->mode == Mode::List) this->refreshRoomList();
        if(this->mode == Mode::Room){
            this->refreshTitle();
            const ChatProto::Room* r = this->client.findRoom(this->client.room());
            this->invite_button->setVisible(r && r->is_private);
        }
        this->refreshStatus();
    }
    if(this->client.messagesRevision() != this->seen_msgs_rev){
        this->seen_msgs_rev = this->client.messagesRevision();
        if(this->mode == Mode::Room){
            this->log_view->refresh();
            this->refreshPlaceholder();
        }
    }
    if(this->client.statusRevision() != this->seen_status_rev){
        this->seen_status_rev = this->client.statusRevision();
        this->refreshStatus();
    }
    if(this->client.sending() != this->seen_sending){
        this->refreshSendButton();
    }
}

void ChatScene::onExit(){
    //onUpdate() が来なくなるので、通信を打ち切って接続(TLSの約40KB)も返す
    this->client.stop();
    if(this->input && this->input->getText()) this->draft.assign(*this->input->getText());
    //検索の画面は結果を持ち越さないので、戻ってきたら一覧から
    if(this->mode == Mode::Search) this->mode = Mode::List;
    this->pending_invite_dialog = false;

    this->back_button = nullptr;
    this->refresh_button = nullptr;
    this->invite_button = nullptr;
    this->search_button = nullptr;
    this->code_button = nullptr;
    this->title_label = nullptr;
    this->room_list = nullptr;
    this->log_view = nullptr;
    this->input = nullptr;
    this->send_button = nullptr;
    this->status_label = nullptr;
}
