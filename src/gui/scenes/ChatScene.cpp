#include "gui/scenes/ChatScene.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Log_Functions.hpp"

#include <cstdio>
#include <cstring>

static_assert(ChatLogView::kMaxEntries >= ChatClient::kMaxMessages,
              "ChatLogView が ChatClient の発言を全部並べられない");

// 入力欄の高さ(16pxの文字 + 枠と余白)
static constexpr int kInputH = 22;

void ChatScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;

    // ---- 上部の行: [戻る] タイトル [更新] ----
    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([this](){
        if(this->mode == Mode::Room) this->backToList();
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
        this->client.refreshNow();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->refresh_button);

    const int title_x = this->back_button->getLocalRect().x + this->back_button->getLocalRect().w + MARGIN * 2;
    const int title_w = this->refresh_button->getLocalRect().x - MARGIN * 2 - title_x;
    this->title_label = new Label<PICO_STR_M>(title_x, y0, "");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxWidth(title_w);
    this->title_label->setMaxHeight(FontFn::GetFontSize(FontFn::Small) + 2);
    this->title_label->setY(y0 + (this->row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->title_label);

    this->body_top = y0 + this->row_h + MARGIN;
    this->input_row_y = content.y + content.h - MARGIN - kInputH;

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
                                     ChatClient::kMaxRooms);
    this->room_list->setFontSize(FontFn::Small);
    //部屋は1回のタップで開く(選ぶだけの操作が無いので)
    this->room_list->setOnSelectItem([this](int index, bool){
        if(index >= 0 && index < this->list_count) this->openRoom(this->list_room_ids[index]);
    });
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
    this->seen_sending = !this->client.sending();
    this->frames_since_enter = 0;

    this->applyMode();
}

void ChatScene::applyMode(){
    const Rect content = Scene::contentRect();
    const bool in_room = (this->mode == Mode::Room);
    const int line_h = FontFn::GetFontSize(FontFn::Small) + 2;

    this->room_list->setVisible(!in_room);
    this->log_view->setVisible(in_room);
    this->input->setVisible(in_room);
    this->send_button->setVisible(in_room);

    //状態の行は、部屋の中では入力欄の上に1行、一覧では下端に2行(設定の案内が長いので)
    int status_lines = in_room ? 1 : 2;
    int bottom = in_room ? this->input_row_y - MARGIN : content.y + content.h - MARGIN;
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
        FixedString<PICO_STR_M> t("# ");
        t.append(r ? r->name.c_str() : "");
        this->title_label->setText(t);
    }else{
        this->title_label->setText("チャット");
    }
}

void ChatScene::refreshRoomList(){
    if(!this->room_list) return;
    this->room_list->clear();
    this->list_count = 0;
    for(int i = 0; i < this->client.roomCount() && i < ChatClient::kMaxRooms; i++){
        const ChatProto::Room& r = this->client.roomAt(i);
        ScrollListTools::Item item;
        item.text.append("# ");
        item.text.append(r.name);
        if(r.unread > 0){
            item.text.appendFormat("  (%lu)", (unsigned long)r.unread);
            item.color = PICO_RED;
        }
        this->room_list->add(item);
        this->list_room_ids[this->list_count++] = r.id;
    }
}

void ChatScene::refreshStatus(){
    if(!this->status_label) return;
    const ChatClient::State s = this->client.state();
    const bool bad = (s == ChatClient::State::Error || s == ChatClient::State::AuthError
                   || s == ChatClient::State::NotConfigured || s == ChatClient::State::Offline);
    this->status_label->setTextColor(bad ? PICO_RED : PICO_DARKGREY);

    const char* text = this->client.statusText();
    if(!bad && this->mode == Mode::List && this->client.roomCount() == 0 && this->client.roomsRevision() != 0){
        text = "部屋がありません。Webから作ってください";
    }
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
    this->applyMode();
}

void ChatScene::backToList(){
    KeyboardFunctions::HideAll();
    this->client.setRoom(0);
    this->mode = Mode::List;
    this->applyMode();
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

    this->client.update(NetworkFunctions::IsConnected());

    if(this->client.roomsRevision() != this->seen_rooms_rev){
        this->seen_rooms_rev = this->client.roomsRevision();
        this->refreshRoomList();
        if(this->mode == Mode::Room) this->refreshTitle();
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

    this->back_button = nullptr;
    this->refresh_button = nullptr;
    this->title_label = nullptr;
    this->room_list = nullptr;
    this->log_view = nullptr;
    this->input = nullptr;
    this->send_button = nullptr;
    this->status_label = nullptr;
}
