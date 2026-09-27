#include "chat/Chat_Client.hpp"

#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdio>
#include <cstring>

namespace {
    // millis() の巻き戻り(約49日)を跨いでも正しく比べる
    bool Reached(uint32_t now, uint32_t at){
        return (int32_t)(now - at) >= 0;
    }
}

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

bool ChatClient::configure(const char* server, const char* token, uint32_t poll_ms){
    configured_ = false;
    server_ = Url{};
    auth_.clear();

    if(!server || !*server || !token || !*token){
        setStatus(State::NotConfigured, "/sys/chat.cfg に server と token を書いてください");
        return false;
    }
    if(!UrlTools::Parse(server_, server)){
        setStatus(State::NotConfigured, "chat.cfg の server が URL ではありません");
        return false;
    }
    //末尾の / は落としておく(後ろへ "/api/v1/..." を足すため)。
    //逆プロキシで /chat/ の下に置いた場合もパスごと書けば動く
    while(server_.path.length() > 0 && server_.path.c_str()[server_.path.length() - 1] == '/'){
        FixedString<PICO_STR_LL> trimmed;
        trimmed.assign(server_.path.c_str(), server_.path.length() - 1);
        server_.path = trimmed;
    }
    //トークンは base64url。ヘッダへ入れるので、それ以外の文字は受け付けない
    for(const char* p = token; *p; p++){
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                     || c == '-' || c == '_';
        if(!ok){
            setStatus(State::NotConfigured, "chat.cfg の token に使えない文字があります");
            return false;
        }
    }
    if(!auth_.assign("Authorization: Bearer ") || !auth_.append(token)
       || !req_.setExtraHeader(auth_.c_str())){
        setStatus(State::NotConfigured, "chat.cfg の token が長すぎます");
        return false;
    }

    poll_ms_ = (poll_ms < 1000) ? 1000 : poll_ms;
    req_.setKeepAlive(true);
    configured_ = true;
    wait_manual_ = false;
    backoff_ms_ = 0;
    next_poll_ms_ = millis();
    setStatus(State::Ok, "");
    return true;
}

bool ChatClient::loadConfig(){
    if(!OSData::SD_usable){
        setStatus(State::NotConfigured, "SDカードがありません");
        configured_ = false;
        return false;
    }

    char server[PICO_Config::kConfigMaxValueLen] = "";
    char token[PICO_Config::kConfigMaxValueLen] = "";
    int poll = (int)kDefaultPollMs;

    const bool opened = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_CHAT_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "server") == 0){
                strncpy(server, value, sizeof(server) - 1);
            }else if(strcmp(key, "token") == 0){
                strncpy(token, value, sizeof(token) - 1);
            }else if(strcmp(key, "poll-ms") == 0){
                int v = 0;
                if(PICO_Config::ConfigValue::AsInt(value, v) && v > 0) poll = v;
            }
        });
    if(!opened){
        setStatus(State::NotConfigured, "/sys/chat.cfg がありません");
        configured_ = false;
        return false;
    }
    return configure(server, token, (uint32_t)poll);
}

void ChatClient::setStatus(State s, const char* text){
    const bool changed = (s != state_) || strcmp(status_.c_str(), text ? text : "") != 0;
    state_ = s;
    status_.assign(text ? text : "");
    if(changed) status_rev_++;
}

// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

void ChatClient::clearMessages(){
    msg_head_ = 0;
    msg_count_ = 0;
    last_id_ = 0;
    loaded_ = false;
    msgs_rev_++;
}

void ChatClient::setRoom(uint32_t room_id){
    if(room_id == room_) return;
    room_ = room_id;
    clearMessages();
    //開いたらすぐ取りに行く。一覧へ戻ったときも未読の数を取り直す
    next_poll_ms_ = millis();
}

bool ChatClient::send(const char* text){
    if(!configured_ || room_ == 0 || pending_send_ || kind_ == Kind::Send) return false;
    if(!text) return false;
    const size_t n = strlen(text);
    if(n == 0 || n > ChatProto::kMaxTextBytes) return false;
    if(!send_text_.assign(text)) return false;
    pending_send_ = true;
    send_room_ = room_;
    return true;
}

void ChatClient::refreshNow(){
    wait_manual_ = false;
    backoff_ms_ = 0;
    next_poll_ms_ = millis();
}

void ChatClient::stop(){
    req_.cancel();
    req_.closeConnection();
    kind_ = Kind::None;
}

bool ChatClient::queueAction(Action a, uint32_t room_id, const char* text){
    if(!configured_ || actionBusy()) return false;
    if(!action_text_.assign(text ? text : "")) return false;
    pending_action_ = a;
    action_room_ = room_id;
    return true;
}

bool ChatClient::search(const char* query){
    return queueAction(Action::Search, 0, query);
}

bool ChatClient::joinOpen(uint32_t room_id){
    if(room_id == 0) return false;
    return queueAction(Action::JoinOpen, room_id, nullptr);
}

bool ChatClient::joinByCode(const char* code){
    if(!code || !*code) return false;
    return queueAction(Action::JoinCode, 0, code);
}

bool ChatClient::requestInvite(){
    const ChatProto::Room* r = findRoom(room_);
    //参加コードはプライベートチャットだけ(オープンチャットは検索から入れる)
    if(!r || !r->is_private) return false;
    return queueAction(Action::Invite, room_, nullptr);
}

const ChatProto::Room* ChatClient::findRoom(uint32_t id) const {
    for(int i = 0; i < room_count_; i++){
        if(rooms_[i].id == id) return &rooms_[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 通信
// ---------------------------------------------------------------------------

bool ChatClient::begin(Kind kind, HttpRequest::Method method, const char* path, const char* query,
                       const void* body, size_t body_len){
    Url url = server_;
    url.query.clear();
    if(!url.path.append(path) || (query && !url.query.assign(query))){
        failLater("URLが長すぎます");
        return false;
    }

    kind_ = kind;
    lines_.owner = this;
    lines_.resetLines();
    error_line_.clear();
    error_line_done_ = false;
    new_room_count_ = 0;
    msgs_changed_ = false;
    joined_ok_ = false;
    invite_ok_ = false;
    if(kind == Kind::Search) search_count_ = 0;

    return req_.begin(url, method, this, body, body_len,
                      body ? "text/plain; charset=utf-8" : nullptr);
}

bool ChatClient::write(const void* data, size_t len){
    const int code = req_.response().statusCode();
    if(code >= 200 && code < 300) return lines_.write(data, len);

    //失敗の応答は、本文の1行目を理由として取っておく(サーバは日本語の1行を返す)
    const char* p = (const char*)data;
    for(size_t i = 0; i < len && !error_line_done_; i++){
        if(p[i] == '\n' || p[i] == '\r'){
            error_line_done_ = true;
            break;
        }
        if(!error_line_.append(p[i])) error_line_done_ = true;
    }
    return true;
}

void ChatClient::Lines::onLine(char* line, size_t){
    if(owner) owner->onLine(line);
}

void ChatClient::onLine(char* line){
    switch(kind_){
        case Kind::Rooms: {
            if(new_room_count_ >= kMaxRooms) return;
            ChatProto::Room r;
            if(ChatProto::ParseRoom(line, r)) new_rooms_[new_room_count_++] = r;
            break;
        }
        case Kind::Messages: {
            //要求を投げた後に部屋を変えていたら、前の部屋の発言なので捨てる
            if(req_room_ != room_) return;
            ChatProto::Message m;
            if(ChatProto::ParseMessage(line, m)) addMessage(m);
            break;
        }
        case Kind::Search: {
            if(search_count_ >= kMaxSearchHits) return;
            ChatProto::SearchHit h;
            if(ChatProto::ParseSearchHit(line, h)) search_hits_[search_count_++] = h;
            break;
        }
        case Kind::JoinOpen:
        case Kind::JoinCode:
            if(!joined_ok_) joined_ok_ = ChatProto::ParseJoined(line, joined_);
            break;
        case Kind::Invite:
            if(!invite_ok_) invite_ok_ = ChatProto::ParseInvite(line, invite_code_, invite_expires_);
            break;
        default:
            break;
    }
}

void ChatClient::addMessage(const ChatProto::Message& m){
    //同じ発言を2度並べない(取り直しの境目で重なった場合)
    if(m.id <= last_id_) return;

    if(msg_count_ < kMaxMessages){
        msgs_[(msg_head_ + msg_count_) % kMaxMessages] = m;
        msg_count_++;
    }else{
        //一番古いものを押し出す
        msgs_[msg_head_] = m;
        msg_head_ = (msg_head_ + 1) % kMaxMessages;
    }
    last_id_ = m.id;
    msgs_changed_ = true;
}

void ChatClient::failLater(const char* why){
    backoff_ms_ = (backoff_ms_ == 0) ? kMinBackoffMs : backoff_ms_ * 2;
    if(backoff_ms_ > kMaxBackoffMs) backoff_ms_ = kMaxBackoffMs;
    next_poll_ms_ = millis() + backoff_ms_;
    setStatus(State::Error, why);
}

void ChatClient::finishAction(Kind kind, bool ok, const char* why){
    switch(kind){
        case Kind::Search:   last_action_ = Action::Search; break;
        case Kind::JoinOpen: last_action_ = Action::JoinOpen; break;
        case Kind::JoinCode: last_action_ = Action::JoinCode; break;
        case Kind::Invite:   last_action_ = Action::Invite; break;
        default: return;
    }
    last_action_ok_ = ok;
    action_msg_.assign(ok ? "" : (why ? why : ""));
    if(!ok && kind == Kind::Search) search_count_ = 0;
    action_rev_++;
}

void ChatClient::beginAction(){
    const Action a = pending_action_;
    pending_action_ = Action::None;

    char path[48];
    FixedString<PICO_STR_LL> query;
    const char* body = nullptr;
    size_t body_len = 0;
    Kind kind = Kind::None;
    const char* q = nullptr;

    switch(a){
        case Action::Search: {
            kind = Kind::Search;
            snprintf(path, sizeof(path), "/api/v1/rooms/search");
            FixedString<PICO_STR_LL> enc;
            if(!UrlTools::EncodeComponent(enc, action_text_.c_str())
               || !query.appendFormat("limit=%d&q=", kMaxSearchHits) || !query.append(enc)){
                finishAction(kind, false, "検索語が長すぎます");
                return;
            }
            q = query.c_str();
            break;
        }
        case Action::JoinOpen:
            kind = Kind::JoinOpen;
            snprintf(path, sizeof(path), "/api/v1/rooms/%lu/join", (unsigned long)action_room_);
            break;
        case Action::JoinCode:
            kind = Kind::JoinCode;
            snprintf(path, sizeof(path), "/api/v1/join");
            body = action_text_.c_str();
            body_len = action_text_.length();
            break;
        case Action::Invite:
            kind = Kind::Invite;
            snprintf(path, sizeof(path), "/api/v1/rooms/%lu/invites", (unsigned long)action_room_);
            break;
        default:
            return;
    }

    const HttpRequest::Method method = (kind == Kind::Search) ? HttpRequest::Method::GET : HttpRequest::Method::POST;
    begin(kind, method, path, q, body, body_len);
    //URLが組み立てられずに始まらなかった(kind_ が立っていない)なら、ここで失敗を返す
    if(kind_ == Kind::None) finishAction(kind, false, "URLが長すぎます");
}

void ChatClient::finish(){
    const Kind kind = kind_;
    kind_ = Kind::None;

    if(req_.getStatus() != TaskTools::SUCCESS){
        FixedString<PICO_STR_LL> why("通信に失敗: ");
        why.append(req_.failureToStr());
        LOG_APP_WARN("チャット: %s", why.c_str());
        //送信は失敗したら諦めず、次の機会にもう一度送る(入力し直させない)
        if(kind == Kind::Send) pending_send_ = true;
        //利用者の操作は勝手にやり直さない(押し直してもらう)
        if(isActionKind(kind)) finishAction(kind, false, why.c_str());
        failLater(why.c_str());
        return;
    }

    const int code = req_.response().statusCode();
    if(code < 200 || code >= 300){
        FixedString<PICO_STR_LL> why;
        if(!error_line_.empty()) why.assign(error_line_);
        else why.appendFormat("サーバの応答が %d です", code);
        LOG_APP_WARN("チャット: HTTP %d %s", code, why.c_str());

        if(code == 401){
            //トークンが違う。何度叩いても同じなので、利用者が直して「更新」するまで待つ
            wait_manual_ = true;
            pending_send_ = false;
            pending_action_ = Action::None;
            if(isActionKind(kind)) finishAction(kind, false, why.c_str());
            setStatus(State::AuthError, why.c_str());
            return;
        }
        if(isActionKind(kind)){
            //参加コードが違う・権限が無い等。通信は生きているので間は空けず、理由だけ返す
            finishAction(kind, false, why.c_str());
            return;
        }
        if(kind == Kind::Send){
            //長すぎる等、送り直しても通らない理由。送信待ちは捨てて理由を見せる
            pending_send_ = false;
            backoff_ms_ = 0;
            next_poll_ms_ = millis();
            setStatus(State::Error, why.c_str());
            return;
        }
        if((code == 403 || code == 404) && kind == Kind::Messages){
            //追い出された/部屋が消えた。一覧から外して部屋を閉じ、一覧を取り直す(画面は一覧へ戻す)
            if(req_room_ == room_ && room_ != 0){
                removeRoom(room_);
                room_ = 0;
                clearMessages();
                room_lost_rev_++;
            }
            backoff_ms_ = 0;
            next_poll_ms_ = millis();
            setStatus(State::Error, why.c_str());
            return;
        }
        failLater(why.c_str());
        return;
    }

    lines_.flush();
    backoff_ms_ = 0;
    setStatus(State::Ok, "");

    switch(kind){
        case Kind::Rooms:
            for(int i = 0; i < new_room_count_; i++) rooms_[i] = new_rooms_[i];
            room_count_ = new_room_count_;
            rooms_rev_++;
            next_poll_ms_ = millis() + kRoomsPollMs;
            break;
        case Kind::Messages:
            if(req_room_ == room_){
                if(!loaded_){ loaded_ = true; msgs_changed_ = true; }
                if(msgs_changed_) msgs_rev_++;
            }
            next_poll_ms_ = millis() + poll_ms_;
            //部屋を変えていたら、新しい部屋をすぐ取りに行く
            if(req_room_ != room_) next_poll_ms_ = millis();
            break;
        case Kind::Send:
            //自分の発言をすぐ見せる
            next_poll_ms_ = millis();
            break;
        case Kind::Search:
            finishAction(kind, true, nullptr);
            break;
        case Kind::JoinOpen:
        case Kind::JoinCode:
            if(!joined_ok_){
                finishAction(kind, false, "サーバの応答を読めませんでした");
                break;
            }
            //一覧を取り直すまで待たずに足しておく(開いた部屋の名前を出すため)
            upsertRoom(joined_);
            joined_room_ = joined_.id;
            finishAction(kind, true, nullptr);
            break;
        case Kind::Invite:
            finishAction(kind, invite_ok_, invite_ok_ ? nullptr : "サーバの応答を読めませんでした");
            break;
        default:
            break;
    }
}

void ChatClient::upsertRoom(const ChatProto::Room& r){
    for(int i = 0; i < room_count_; i++){
        if(rooms_[i].id == r.id){
            rooms_[i].name = r.name;
            rooms_[i].is_private = r.is_private;
            rooms_[i].role = r.role;
            rooms_rev_++;
            return;
        }
    }
    if(room_count_ >= kMaxRooms) return;
    rooms_[room_count_++] = r;
    rooms_rev_++;
}

void ChatClient::removeRoom(uint32_t id){
    for(int i = 0; i < room_count_; i++){
        if(rooms_[i].id != id) continue;
        for(int j = i + 1; j < room_count_; j++) rooms_[j - 1] = rooms_[j];
        room_count_--;
        rooms_rev_++;
        return;
    }
}

void ChatClient::update(bool network_up){
    if(!configured_) return;

    if(kind_ != Kind::None){
        req_.update();
        if(req_.getStatus() != TaskTools::PROCESSING) finish();
        return;
    }

    if(!network_up){
        if(state_ != State::Offline) req_.closeConnection();
        setStatus(State::Offline, "Wi-Fiに接続していません");
        return;
    }
    if(state_ == State::Offline){
        //繋がったらすぐ取りに行く
        setStatus(State::Ok, "");
        next_poll_ms_ = millis();
    }
    if(wait_manual_) return;

    const uint32_t now = millis();
    const bool due = Reached(now, next_poll_ms_);
    //送信は問い合わせの間隔を待たずにすぐ送る(失敗して間を空けている間だけは待つ)
    const bool send_now = pending_send_ && (backoff_ms_ == 0 || due);

    if(send_now){
        pending_send_ = false;
        char path[48];
        snprintf(path, sizeof(path), "/api/v1/rooms/%lu/messages", (unsigned long)send_room_);
        begin(Kind::Send, HttpRequest::Method::POST, path, nullptr, send_text_.c_str(), send_text_.length());
        return;
    }

    //利用者の操作(検索・参加・招待)は、押されたらすぐ投げる
    if(pending_action_ != Action::None){
        beginAction();
        return;
    }
    if(!due) return;

    if(room_ != 0){
        char path[48];
        char query[48];
        snprintf(path, sizeof(path), "/api/v1/rooms/%lu/messages", (unsigned long)room_);
        if(last_id_ == 0) snprintf(query, sizeof(query), "limit=%d", kMaxMessages);
        else              snprintf(query, sizeof(query), "after=%lu&limit=%d", (unsigned long)last_id_, kMaxMessages);
        req_room_ = room_;
        begin(Kind::Messages, HttpRequest::Method::GET, path, query);
        return;
    }

    begin(Kind::Rooms, HttpRequest::Method::GET, "/api/v1/rooms", nullptr);
}
