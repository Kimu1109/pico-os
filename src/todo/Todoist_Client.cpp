#include "todo/Todoist_Client.hpp"

#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "util/Secret_Cipher.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <cstdio>
#include <cstring>

namespace {
    // millis() の巻き戻り(約49日)を跨いでも正しく比べる
    bool Reached(uint32_t now, uint32_t at){
        return (int32_t)(now - at) >= 0;
    }

    // Todoistのフィルタの書き方(https://todoist.com/help/articles/introduction-to-filters)。
    // 英語で書いておけば、利用者の言語設定に関わらず通る
    constexpr const char* kTodayFilter = "today | overdue";
    constexpr const char* kWeekFilter = "overdue | next 7 days";

    constexpr const char* kTokenPurpose = "todoist-token";
    constexpr size_t kMaxTokenBytes = 80;
}

// ---------------------------------------------------------------------------
// 設定
// ---------------------------------------------------------------------------

bool TodoistClient::configure(const char* token, const char* api){
    configured_ = false;
    api_ = Url{};
    auth_.clear();

    if(!token || !*token){
        setStatus(State::NotConfigured, "Todoistのトークンがありません。[設定] から入れてください");
        return false;
    }
    if(!api || !*api) api = kDefaultApi;
    if(!UrlTools::Parse(api_, api)){
        setStatus(State::NotConfigured, "todoist.cfg の api が URL ではありません");
        return false;
    }
    //末尾の / は落としておく(後ろへ "/api/v1/..." を足すため)
    while(api_.path.length() > 0 && api_.path.c_str()[api_.path.length() - 1] == '/'){
        FixedString<PICO_STR_LL> trimmed;
        trimmed.assign(api_.path.c_str(), api_.path.length() - 1);
        api_.path = trimmed;
    }
    //ヘッダへそのまま入れるので、英数字と - _ 以外は受け付けない(Todoistのトークンは16進40文字)
    const size_t n = strlen(token);
    if(n > kMaxTokenBytes){
        setStatus(State::NotConfigured, "トークンが長すぎます");
        return false;
    }
    for(const char* p = token; *p; p++){
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                     || c == '-' || c == '_';
        if(!ok){
            setStatus(State::NotConfigured, "トークンに使えない文字があります");
            return false;
        }
    }
    if(!auth_.assign("Authorization: Bearer ") || !auth_.append(token)
       || !req_.setExtraHeader(auth_.c_str())){
        setStatus(State::NotConfigured, "トークンが長すぎます");
        return false;
    }

    req_.setKeepAlive(true);
    configured_ = true;
    wait_manual_ = false;
    backoff_ms_ = 0;
    next_poll_ms_ = millis();
    setStatus(State::Ok, "");
    return true;
}

void TodoistClient::setFilters(const char* today, const char* week){
    today_filter_.assign((today && *today) ? today : "");
    week_filter_.assign((week && *week) ? week : "");
}

const TodoistClient::Filter& TodoistClient::filterFor(View v) const {
    static Filter today_default(kTodayFilter);
    static Filter week_default(kWeekFilter);
    if(v == View::Today) return today_filter_.empty() ? today_default : today_filter_;
    return week_filter_.empty() ? week_default : week_filter_;
}

bool TodoistClient::loadConfig(){
    if(!OSData::SD_usable){
        setStatus(State::NotConfigured, "SDカードがありません");
        configured_ = false;
        return false;
    }

    char token[PICO_Config::kConfigMaxValueLen] = "";
    char api[PICO_Config::kConfigMaxValueLen] = "";
    char today[PICO_Config::kConfigMaxValueLen] = "";
    char week[PICO_Config::kConfigMaxValueLen] = "";
    bool token_plain = false;
    reminders_ = true;
    remind_before_min_ = 0;

    const bool opened = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_TODOIST_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "token") == 0){
                token_plain = strncmp(value, PICO_Secret::kPrefix, PICO_Secret::kPrefixLen) != 0;
                if(!PICO_Secret::Decrypt(kTokenPurpose, value, token, sizeof(token))) token[0] = '\0';
            }else if(strcmp(key, "api") == 0){
                strncpy(api, value, sizeof(api) - 1);
            }else if(strcmp(key, "today-filter") == 0){
                strncpy(today, value, sizeof(today) - 1);
            }else if(strcmp(key, "week-filter") == 0){
                strncpy(week, value, sizeof(week) - 1);
            }else if(strcmp(key, "reminders") == 0){
                bool b = true;
                if(PICO_Config::ConfigValue::AsBool(value, b)) reminders_ = b;
            }else if(strcmp(key, "remind-before-min") == 0){
                int v = 0;
                if(PICO_Config::ConfigValue::AsInt(value, v) && v >= 0 && v <= 24 * 60) remind_before_min_ = v;
            }
        });
    if(!opened){
        setStatus(State::NotConfigured, "Todoistのトークンがありません。[設定] から入れてください");
        configured_ = false;
        return false;
    }
    setFilters(today, week);
    if(!configure(token, api)) return false;

    //母艦で平文のまま書かれていたら、暗号化して書き直す(SDだけを落としたときに読まれないように)
    if(token_plain && token[0]){
        if(SaveToken(token)) LOG_APP_MSG("Todoist: トークンを暗号化して保存し直しました");
    }
    return true;
}

bool TodoistClient::SaveToken(const char* token){
    if(!token || !*token) return false;
    char enc[PICO_Config::kConfigMaxValueLen];
    if(!PICO_Secret::Encrypt(kTokenPurpose, token, enc, sizeof(enc))){
        LOG_APP_WARN("Todoist: トークンを暗号化できませんでした(長すぎます)");
        return false;
    }
    return PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_TODOIST_CFG, "token", enc);
}

void TodoistClient::setStatus(State s, const char* text){
    const bool changed = (s != state_) || strcmp(status_.c_str(), text ? text : "") != 0;
    state_ = s;
    status_.assign(text ? text : "");
    if(changed) status_rev_++;
}

// ---------------------------------------------------------------------------
// 操作
// ---------------------------------------------------------------------------

void TodoistClient::setView(View v){
    if(v == view_) return;
    view_ = v;
    task_count_ = 0;
    loaded_ = false;
    truncated_ = false;
    tasks_rev_++;
    //取り直し中なら、終わった後に新しい表示で取り直す(finishList が見分ける)
    page_ = 0;
    cursor_.clear();
    next_poll_ms_ = millis();
    backoff_ms_ = 0;
}

void TodoistClient::refreshNow(){
    wait_manual_ = false;
    page_ = 0;
    cursor_.clear();
    backoff_ms_ = 0;
    next_poll_ms_ = millis();
}

bool TodoistClient::add(const char* content, const char* due_string){
    if(!configured_ || actionBusy() || !content || !*content) return false;
    if(!add_content_.assign(content)) return false;
    if(!add_due_.assign(due_string ? due_string : "")) return false;
    pending_ = Action::Add;
    return true;
}

bool TodoistClient::close(const char* task_id){
    if(!configured_ || actionBusy() || !task_id || !*task_id) return false;
    //パスへそのまま入れるので、idは英数字と - _ だけ
    for(const char* p = task_id; *p; p++){
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                     || c == '-' || c == '_';
        if(!ok) return false;
    }
    if(!close_id_.assign(task_id)) return false;
    pending_ = Action::Close;
    return true;
}

void TodoistClient::stop(){
    req_.cancel();
    req_.closeConnection();
    kind_ = Kind::None;
}

const Todoist::Task* TodoistClient::findTask(const char* id) const {
    if(!id) return nullptr;
    for(int i = 0; i < task_count_; i++){
        if(strcmp(tasks_[i].id.c_str(), id) == 0) return &tasks_[i];
    }
    return nullptr;
}

void TodoistClient::removeTask(const char* id){
    for(int i = 0; i < task_count_; i++){
        if(strcmp(tasks_[i].id.c_str(), id) != 0) continue;
        for(int j = i + 1; j < task_count_; j++) tasks_[j - 1] = tasks_[j];
        task_count_--;
        tasks_rev_++;
        return;
    }
}

// ---------------------------------------------------------------------------
// 通信
// ---------------------------------------------------------------------------

bool TodoistClient::begin(Kind kind, HttpRequest::Method method, const char* path, const char* query,
                          const void* body, size_t body_len){
    Url url = api_;
    url.query.clear();
    if(!url.path.append(path) || (query && !url.query.assign(query))){
        failLater("URLが長すぎます");
        return false;
    }

    kind_ = kind;
    parse_ok_ = true;
    err_.begin();
    return req_.begin(url, method, this, body, body_len,
                      body ? "application/json" : nullptr);
}

bool TodoistClient::beginList(){
    //最初のページなら受信中の一覧を空にする。続きのページは後ろへ足す
    if(page_ == 0){
        new_count_ = 0;
        new_truncated_ = false;
        req_view_ = view_;
    }
    parser_.begin(new_tasks_, kMaxTasks, new_count_, utc_offset_);

    FixedString<PICO_STR_512B> query;
    const char* path;
    if(req_view_ == View::All){
        path = "/api/v1/tasks";
    }else{
        path = "/api/v1/tasks/filter";
        FixedString<PICO_STR_256B> enc;
        if(!UrlTools::EncodeComponent(enc, filterFor(req_view_).c_str())
           || !query.append("query=") || !query.append(enc) || !query.append("&")){
            failLater("フィルタが長すぎます");
            return false;
        }
    }
    query.appendFormat("limit=%d", kMaxTasks - new_count_);
    if(!cursor_.empty()){
        FixedString<PICO_STR_256B> enc;
        if(!UrlTools::EncodeComponent(enc, cursor_.c_str()) || !query.append("&cursor=") || !query.append(enc)){
            failLater("URLが長すぎます");
            return false;
        }
    }
    return begin(Kind::List, HttpRequest::Method::GET, path, query.c_str());
}

void TodoistClient::beginAction(){
    const Action a = pending_;
    pending_ = Action::None;

    if(a == Action::Close){
        char path[64];
        snprintf(path, sizeof(path), "/api/v1/tasks/%s/close", close_id_.c_str());
        begin(Kind::Close, HttpRequest::Method::POST, path, nullptr);
        if(kind_ == Kind::None) finishAction(Kind::Close, false, "URLが長すぎます");
        return;
    }
    if(a != Action::Add) return;

    //{"content": "...", "due_string": "...", "due_lang": "ja"}
    body_.clear();
    bool ok = body_.append("{\"content\":") && Todoist::AppendJsonString(body_, add_content_.c_str());
    if(ok && !add_due_.empty()){
        //日付の自然言語は日本語で読ませる("明日 15時" "毎週月曜")
        ok = body_.append(",\"due_string\":") && Todoist::AppendJsonString(body_, add_due_.c_str())
          && body_.append(",\"due_lang\":\"ja\"");
    }
    ok = ok && body_.append("}");
    if(!ok){
        finishAction(Kind::Add, false, "タスクの名前が長すぎます");
        return;
    }
    begin(Kind::Add, HttpRequest::Method::POST, "/api/v1/tasks", nullptr, body_.c_str(), body_.length());
    if(kind_ == Kind::None) finishAction(Kind::Add, false, "URLが長すぎます");
}

bool TodoistClient::write(const void* data, size_t len){
    const int code = req_.response().statusCode();
    if(code < 200 || code >= 300){
        err_.feed(data, len);
        return true;
    }
    if(kind_ == Kind::List && parse_ok_){
        //読めないJSONが来たら、残りは読み捨てて失敗にする(finishList)
        if(!parser_.feed(data, len)) parse_ok_ = false;
    }
    //追加の応答(作ったタスク)は読まない。一覧を取り直せば並ぶ
    return true;
}

void TodoistClient::failLater(const char* why){
    backoff_ms_ = (backoff_ms_ == 0) ? kMinBackoffMs : backoff_ms_ * 2;
    if(backoff_ms_ > kMaxBackoffMs) backoff_ms_ = kMaxBackoffMs;
    next_poll_ms_ = millis() + backoff_ms_;
    page_ = 0;
    cursor_.clear();
    setStatus(State::Error, why);
}

void TodoistClient::finishAction(Kind kind, bool ok, const char* why){
    if(kind == Kind::Add) last_action_ = Action::Add;
    else if(kind == Kind::Close) last_action_ = Action::Close;
    else return;
    last_action_ok_ = ok;
    action_msg_.assign(ok ? "" : (why ? why : ""));
    action_rev_++;
}

void TodoistClient::finishList(){
    if(!parse_ok_ || !parser_.complete()){
        FixedString<PICO_STR_LL> why("応答を読めませんでした: ");
        why.append(parse_ok_ ? "途中で終わっています" : parser_.error());
        LOG_APP_WARN("Todoist: %s", why.c_str());
        failLater(why.c_str());
        return;
    }

    //取っている間に表示を変えていたら、この結果は捨てて新しい表示で取り直す
    if(req_view_ != view_){
        page_ = 0;
        cursor_.clear();
        next_poll_ms_ = millis();
        return;
    }

    new_count_ = parser_.count();
    if(parser_.overflowed()) new_truncated_ = true;
    cursor_.assign(parser_.nextCursor());

    //続きのページがあり、まだ入るなら取りに行く
    if(!cursor_.empty()){
        if(new_count_ >= kMaxTasks){
            new_truncated_ = true;
        }else if(page_ + 1 < kMaxPages){
            page_++;
            next_poll_ms_ = millis(); //次の update() ですぐ続きを取る
            return;
        }else{
            new_truncated_ = true;
        }
    }
    page_ = 0;
    cursor_.clear();

    for(int i = 0; i < new_count_; i++) tasks_[i] = new_tasks_[i];
    task_count_ = new_count_;
    Todoist::SortTasks(tasks_, task_count_);
    truncated_ = new_truncated_;
    loaded_ = true;
    loaded_view_ = req_view_;
    tasks_rev_++;
    next_poll_ms_ = millis() + kRefreshMs;
}

void TodoistClient::finish(){
    const Kind kind = kind_;
    kind_ = Kind::None;
    const bool is_action = (kind == Kind::Add || kind == Kind::Close);

    if(req_.getStatus() != TaskTools::SUCCESS){
        FixedString<PICO_STR_LL> why("通信に失敗: ");
        why.append(req_.failureToStr());
        LOG_APP_WARN("Todoist: %s", why.c_str());
        //利用者の操作は勝手にやり直さない(押し直してもらう)
        if(is_action) finishAction(kind, false, why.c_str());
        failLater(why.c_str());
        return;
    }

    const int code = req_.response().statusCode();
    if(code < 200 || code >= 300){
        FixedString<PICO_STR_LL> why;
        if(*err_.message()) why.appendFormat("%s (HTTP %d)", err_.message(), code);
        else why.appendFormat("サーバの応答が %d です", code);
        LOG_APP_WARN("Todoist: HTTP %d %s", code, why.c_str());

        if(code == 401 || code == 403){
            //トークンが違う。何度叩いても同じなので、利用者が直して「更新」するまで待つ
            wait_manual_ = true;
            pending_ = Action::None;
            page_ = 0;
            cursor_.clear();
            if(is_action) finishAction(kind, false, "トークンが違います");
            setStatus(State::AuthError, "トークンが違います。[設定] から入れ直してください");
            return;
        }
        if(is_action){
            //期限の書き方が読めない(400)・もう無いタスク(404)等。通信は生きているので理由だけ返す
            finishAction(kind, false, why.c_str());
            if(kind == Kind::Close && code == 404){
                //別の端末で消された/完了された。一覧を取り直して消す
                refreshNow();
            }
            return;
        }
        failLater(why.c_str());
        return;
    }

    backoff_ms_ = 0;
    setStatus(State::Ok, "");

    switch(kind){
        case Kind::List:
            finishList();
            break;
        case Kind::Close:
            closed_id_ = close_id_;
            //一覧から先に消しておく(取り直しを待たずに見た目へ反映する)。
            //繰り返しのタスクは次の回へ進むので、取り直すとまた並ぶ
            removeTask(close_id_.c_str());
            finishAction(kind, true, nullptr);
            refreshNow();
            break;
        case Kind::Add:
            finishAction(kind, true, nullptr);
            refreshNow();
            break;
        default:
            break;
    }
}

void TodoistClient::update(bool network_up){
    if(!configured_) return;

    if(kind_ != Kind::None){
        req_.update();
        if(req_.getStatus() != TaskTools::PROCESSING) finish();
        return;
    }

    if(!network_up){
        if(state_ != State::Offline) req_.closeConnection();
        page_ = 0;
        cursor_.clear();
        setStatus(State::Offline, "Wi-Fiに接続していません");
        return;
    }
    if(state_ == State::Offline){
        //繋がったらすぐ取りに行く
        setStatus(State::Ok, "");
        next_poll_ms_ = millis();
    }
    if(wait_manual_) return;

    //利用者の操作は、押されたらすぐ投げる(一覧のページの間にも割り込む)
    if(pending_ != Action::None){
        beginAction();
        return;
    }
    if(!Reached(millis(), next_poll_ms_)) return;
    beginList();
}
