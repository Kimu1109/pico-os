#include "gui/scenes/TodoScene.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "todo/Todo_Reminders.hpp"

#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>

// 一覧の下のボタンの行の高さ
static constexpr int kActionH = 24;
// 選んだタスクを出す欄の行数(名前2行 + 期限/優先度1行)
static constexpr int kDetailLines = 3;
// NTP同期前(1970年付近)は「今日」が分からない
static constexpr int kMinValidYear = 2020;

int32_t TodoScene::LocalUtcOffsetSec(){
    //newlib に tm_gmtoff が無いので、localtime と gmtime の差から求める(CalendarScene と同じ)
    const time_t now = time(nullptr);
    struct tm l, u;
    localtime_r(&now, &l);
    gmtime_r(&now, &u);
    auto secs = [](const struct tm& t) -> int64_t {
        return (int64_t)Todoist::DaysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday) * 86400
             + t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec;
    };
    return (int32_t)(secs(l) - secs(u));
}

int32_t TodoScene::TodayDay(int32_t* now_sec){
    const time_t now = time(nullptr);
    struct tm l;
    localtime_r(&now, &l);
    if(l.tm_year + 1900 < kMinValidYear){
        if(now_sec) *now_sec = 0;
        return -1;
    }
    if(now_sec) *now_sec = l.tm_hour * 3600 + l.tm_min * 60 + l.tm_sec;
    return Todoist::DaysFromCivil(l.tm_year + 1900, l.tm_mon + 1, l.tm_mday);
}

void TodoScene::onEnter(){
    const Rect content = Scene::contentRect();
    const int y0 = content.y + MARGIN;
    const int line_h = FontFn::GetFontSize(FontFn::Small) + 2;

    // ---- 上部の行: [戻る] TODO [設定] [更新] ----
    this->back_button = new Button(content.x + MARGIN, y0, "戻る");
    this->back_button->setFontSize(FontFn::Small);
    this->back_button->setH(20);
    this->back_button->setOnPressEnd([](){ SceneFunctions::Pop(); });
    WidgetFunctions::Add(this->back_button);
    const int row_h = this->back_button->getLocalRect().h;

    this->refresh_button = new Button(0, y0, "更新");
    this->refresh_button->setFontSize(FontFn::Small);
    this->refresh_button->setH(20);
    this->refresh_button->setX(content.x + content.w - MARGIN - this->refresh_button->getLocalRect().w);
    this->refresh_button->setOnPressEnd([this](){
        //設定を書き換えてから押した場合にも効くよう、読み直してから取り直す
        if(this->client.state() == TodoistClient::State::NotConfigured
           || this->client.state() == TodoistClient::State::AuthError){
            this->client.loadConfig();
        }
        this->client.setUtcOffset(LocalUtcOffsetSec());
        this->client.refreshNow();
        this->notice.clear();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->refresh_button);

    this->settings_button = new Button(0, y0, "設定");
    this->settings_button->setFontSize(FontFn::Small);
    this->settings_button->setH(20);
    this->settings_button->setX(this->refresh_button->getLocalRect().x - MARGIN - this->settings_button->getLocalRect().w);
    this->settings_button->setOnPressEnd([this](){ this->openSettings(); });
    WidgetFunctions::Add(this->settings_button);

    const int title_x = this->back_button->getLocalRect().x + this->back_button->getLocalRect().w + MARGIN * 2;
    const int title_w = this->settings_button->getLocalRect().x - MARGIN * 2 - title_x;
    this->title_label = new Label<PICO_STR_M>(title_x, y0, "");
    this->title_label->setFontSize(FontFn::Small);
    this->title_label->setMaxWidth(title_w);
    this->title_label->setMaxHeight(line_h);
    this->title_label->setY(y0 + (row_h - FontFn::GetFontSize(FontFn::Small)) / 2);
    WidgetFunctions::Add(this->title_label);

    // ---- 表示の切り替え ----
    const int tab_y = y0 + row_h + MARGIN;
    this->view_tab = new TabBar(content.x + MARGIN, tab_y, content.w - MARGIN * 2, row_h);
    this->view_tab->addTab("今日");
    this->view_tab->addTab("7日間");
    this->view_tab->addTab("すべて");
    this->view_tab->setSelected((int)this->view);
    this->view_tab->setOnChanged([this](int index){
        this->view = (TodoistClient::View)index;
        this->selected_id.clear();
        this->notice.clear();
        this->client.setView(this->view);
        this->refreshList();
        this->refreshDetail();
        this->refreshStatus();
    });
    WidgetFunctions::Add(this->view_tab);

    // ---- 一番下: [追加] [完了] ----
    const int action_y = content.y + content.h - MARGIN - kActionH;
    const int half_w = (content.w - MARGIN * 3) / 2;
    this->add_button = new Button(content.x + MARGIN, action_y, "追加");
    this->add_button->setFontSize(FontFn::Small);
    this->add_button->setH(kActionH);
    this->add_button->setW(half_w);
    this->add_button->setOnPressEnd([this](){ this->openAddDialog(); });
    WidgetFunctions::Add(this->add_button);

    this->done_button = new Button(content.x + MARGIN * 2 + half_w, action_y, "完了");
    this->done_button->setFontSize(FontFn::Small);
    this->done_button->setH(kActionH);
    this->done_button->setW(half_w);
    this->done_button->setOnPressEnd([this](){ this->confirmClose(); });
    WidgetFunctions::Add(this->done_button);

    // ---- 状態の1行 と 選んだタスクの欄 ----
    const int status_y = action_y - MARGIN - line_h;
    this->status_label = new Label<PICO_STR_LL>(content.x + MARGIN, status_y, "");
    this->status_label->setFontSize(FontFn::Small);
    this->status_label->setMaxWidth(content.w - MARGIN * 2);
    this->status_label->setMaxHeight(line_h);
    this->status_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->status_label);

    const int detail_y = status_y - MARGIN - line_h * kDetailLines;
    this->detail_label = new Label<PICO_STR_512B>(content.x + MARGIN, detail_y, "");
    this->detail_label->setFontSize(FontFn::Small);
    this->detail_label->setMaxWidth(content.w - MARGIN * 2);
    this->detail_label->setMaxHeight(line_h * kDetailLines);
    //タスク名の * や _ をマークアップとして消さない
    this->detail_label->setDisableAutoTextDecoration(true);
    WidgetFunctions::Add(this->detail_label);

    // ---- 一覧 ----
    const int list_y = tab_y + row_h + MARGIN;
    this->task_list = new ScrollList(content.x + MARGIN, list_y, content.w - MARGIN * 2,
                                     detail_y - MARGIN - list_y, TodoistClient::kMaxTasks);
    this->task_list->setFontSize(FontFn::Small);
    this->task_list->setEnableIcon(true);
    this->task_list->setOnSelectItem([this](int index, bool already){ this->onListTap(index, already); });
    WidgetFunctions::Add(this->task_list);

    this->client.loadConfig();
    this->client.setUtcOffset(LocalUtcOffsetSec());
    this->client.setView(this->view);

    this->seen_tasks_rev = this->client.tasksRevision() - 1;
    this->seen_status_rev = this->client.statusRevision() - 1;
    this->seen_action_rev = this->client.actionRevision();
    this->synced_tasks_rev = this->client.tasksRevision();
    this->shown_today = -2;
    this->shown_overdue = -1;
    this->frames_since_enter = 0;
    this->pending_due_dialog = false;

    this->refreshTitle();
    this->refreshList();
    this->refreshDetail();
    this->refreshStatus();
}

void TodoScene::refreshTitle(){
    if(!this->title_label) return;
    FixedString<PICO_STR_M> t("TODO");
    if(this->client.loaded()) t.appendFormat(" (%d)", this->client.taskCount());
    this->title_label->setText(t);
}

int TodoScene::countOverdue(int32_t today, int32_t now_sec) const {
    int n = 0;
    for(int i = 0; i < this->client.taskCount(); i++){
        if(Todoist::IsOverdue(this->client.taskAt(i).due, today, now_sec)) n++;
    }
    return n;
}

void TodoScene::refreshList(){
    if(!this->task_list) return;
    int32_t now_sec = 0;
    const int32_t today = TodayDay(&now_sec);
    this->shown_today = today;
    this->shown_overdue = countOverdue(today, now_sec);

    this->task_list->clear();
    this->list_count = 0;
    int selected_row = -1;
    for(int i = 0; i < this->client.taskCount() && this->list_count < TodoistClient::kMaxTasks; i++){
        const Todoist::Task& t = this->client.taskAt(i);
        ScrollListTools::Item item;
        item.icon = IconID::CheckboxOff;
        if(t.subtask) item.text.append("  ");
        //優先度は ! の数で(P1 = !!!)。色は期限切れに使う
        if(t.priority >= 2){
            for(int k = 1; k < t.priority; k++) item.text.append('!');
            item.text.append(' ');
        }
        if(t.due.has){
            char due[48];
            Todoist::FormatDue(t.due, today, due, sizeof(due));
            item.text.append(due);
            item.text.append(' ');
        }
        item.text.append(t.content);
        if(Todoist::IsOverdue(t.due, today, now_sec)) item.color = PICO_RED;
        this->task_list->add(item);
        if(this->selected_id == t.id) selected_row = this->list_count;
        this->list_task_index[this->list_count++] = i;
    }
    if(selected_row >= 0) this->task_list->setSelectedIndex(selected_row);
    else this->selected_id.clear();
    this->refreshTitle();
}

const Todoist::Task* TodoScene::selectedTask() const {
    if(this->selected_id.empty()) return nullptr;
    return this->client.findTask(this->selected_id.c_str());
}

void TodoScene::refreshDetail(){
    if(!this->detail_label) return;
    const Todoist::Task* t = this->selectedTask();
    if(!t){
        this->detail_label->setTextColor(PICO_DARKGREY);
        if(this->client.loaded() && this->client.taskCount() == 0){
            this->detail_label->setText("タスクはありません。[追加] で足せます");
        }else if(this->client.loaded()){
            this->detail_label->setText("タップで選択、もう一度タップで完了");
        }else{
            this->detail_label->setText("");
        }
        return;
    }

    FixedString<PICO_STR_512B> text;
    text.append(t->content);
    text.append('\n');
    if(t->due.has){
        char due[48];
        Todoist::FormatDue(t->due, TodayDay(), due, sizeof(due));
        text.appendFormat("期限 %s", due);
        //繰り返しは Todoist の書き方("毎日 9:30")をそのまま見せる
        if(t->due.recurring && !t->due.text.empty()) text.appendFormat("(%s)", t->due.text.c_str());
        text.append("  ");
    }else{
        text.append("期限なし  ");
    }
    text.appendFormat("P%d", 5 - t->priority);
    this->detail_label->setTextColor(PICO_BLACK);
    this->detail_label->setText(text);
}

void TodoScene::setNotice(const char* text, int8_t color){
    this->notice.assign(text ? text : "");
    this->notice_color = color;
    this->refreshStatus();
}

void TodoScene::refreshStatus(){
    if(!this->status_label) return;
    using S = TodoistClient::State;
    const S s = this->client.state();
    const bool bad = (s == S::Error || s == S::AuthError || s == S::NotConfigured || s == S::Offline);

    const char* text = this->client.statusText();
    int8_t color = bad ? PICO_RED : PICO_DARKGREY;
    if(!*text && !this->notice.empty()){
        text = this->notice.c_str();
        color = this->notice_color;
    }else if(!*text && !this->client.loaded()){
        text = "読み込み中…";
    }else if(!*text && this->client.truncated()){
        text = "多すぎるので一部だけ表示しています";
    }
    this->status_label->setTextColor(color);
    this->status_label->setText(text);
}

void TodoScene::onListTap(int index, bool already_selected){
    if(index < 0 || index >= this->list_count) return;
    const Todoist::Task& t = this->client.taskAt(this->list_task_index[index]);
    this->selected_id = t.id;
    this->refreshDetail();
    if(already_selected) this->confirmClose();
}

void TodoScene::confirmClose(){
    const Todoist::Task* t = this->selectedTask();
    if(!t){
        this->setNotice("完了にするタスクを選んでください", PICO_RED);
        return;
    }
    if(this->client.actionBusy()){
        this->setNotice("前の操作が終わるまで待ってください", PICO_RED);
        return;
    }
    //MsgDialog は96バイトまで・大きい文字で3行ほどなので、名前は短く切る
    FixedString<PICO_STR_S> name;
    const bool whole = name.assign(t->content.c_str());
    FixedString<PICO_STR_L> text;
    text.appendFormat("「%s%s」を完了にしますか?", name.c_str(), whole ? "" : "…");
    this->confirm_id = t->id;

    auto* dialog = new MsgDialog(text.c_str(), "やめる", "完了");
    dialog->setIconId(IconID::CheckboxOn);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool ok){
        if(ok && !this->confirm_id.empty()){
            if(this->client.close(this->confirm_id.c_str())) this->setNotice("完了にしています…");
            else this->setNotice("前の操作が終わるまで待ってください", PICO_RED);
        }
        this->confirm_id.clear();
        WidgetFunctions::DestroyLater(dialog);
    });
}

void TodoScene::openAddDialog(){
    if(this->client.state() == TodoistClient::State::NotConfigured){
        this->setNotice("先に [設定] でトークンを入れてください", PICO_RED);
        return;
    }
    auto* dialog = new InputDialog("新しいタスク:", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit && !dialog->getInput().empty()){
            this->pending_content.assign(dialog->getInput().c_str());
            this->pending_due_dialog = true;
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void TodoScene::openDueDialog(){
    auto* dialog = new InputDialog("期限(例: 明日 15時。空欄で無し):", true);
    WidgetFunctions::AddDialog(dialog);
    //今日の表示から足すなら、期限は今日にしておく(消せば期限なし)
    if(this->view == TodoistClient::View::Today) dialog->setInput("今日");
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit){
            if(this->client.add(this->pending_content.c_str(), dialog->getInput().c_str())){
                this->setNotice("追加しています…");
            }else{
                this->setNotice(this->client.actionBusy() ? "前の操作が終わるまで待ってください"
                                                          : "タスクを追加できません", PICO_RED);
            }
        }
        this->pending_content.clear();
        WidgetFunctions::DestroyLater(dialog);
    });
}

void TodoScene::openSettings(){
    //Wi-Fiパスワード・チャットのトークンと同じ扱い: 今のトークンは平文で持たないので空欄から始まる
    auto* dialog = new InputDialog("Todoistのトークン(空欄で変更なし):", true);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([this, dialog](bool is_submit){
        if(is_submit && !dialog->getInput().empty()){
            if(TodoistClient::SaveToken(dialog->getInput().c_str())){
                this->client.loadConfig();
                this->client.setUtcOffset(LocalUtcOffsetSec());
                this->client.refreshNow();
                this->setNotice("トークンを保存しました");
            }else{
                this->setNotice("トークンを保存できませんでした", PICO_RED);
            }
        }
        WidgetFunctions::DestroyLater(dialog);
    });
}

void TodoScene::onActionDone(){
    using A = TodoistClient::Action;
    if(!this->client.lastActionOk()){
        this->setNotice(this->client.actionMessage(), PICO_RED);
        return;
    }
    if(this->client.lastAction() == A::Close){
        TodoReminders::Cancel(this->client.lastClosedId());
        if(this->selected_id == this->client.lastClosedId()) this->selected_id.clear();
        this->setNotice("完了にしました");
        this->refreshDetail();
    }else if(this->client.lastAction() == A::Add){
        this->setNotice("追加しました");
    }
}

void TodoScene::syncReminders(){
    if(!this->client.loaded() || this->client.loadedView() != this->view) return;
    if(!this->client.remindersEnabled()){
        if(TodoReminders::Count() > 0) TodoReminders::CancelAll();
        return;
    }
    const int32_t today = TodayDay();
    if(today < 0) return; //時計が合うまで待つ

    const int32_t offset = LocalUtcOffsetSec();
    const int64_t now = (int64_t)time(nullptr);
    //この一覧が網羅している期限の終わり(その日の終わりまで)
    int64_t covered;
    if(this->client.truncated()) covered = now;
    else if(this->view == TodoistClient::View::Today) covered = (int64_t)(today + 1) * 86400 - offset;
    else if(this->view == TodoistClient::View::Week)  covered = (int64_t)(today + 7) * 86400 - offset;
    else covered = INT64_MAX;

    TodoReminders::Sync(&this->client.taskAt(0), this->client.taskCount(), now, covered, offset,
                        this->client.remindBeforeMin(), kAppName);
}

void TodoScene::onUpdate(){
    //通信中はスリープさせない(応答を待っている間に止まらないように)
    if(this->client.busy()) PowerFunctions::KeepAwake();
    //1回描いてから繋ぎに行く(最初の接続はTLSのハンドシェイクで1〜2秒止まるため)
    if(this->frames_since_enter < 2){
        this->frames_since_enter++;
        return;
    }

    if(this->pending_due_dialog){
        this->pending_due_dialog = false;
        this->openDueDialog();
    }

    this->client.update(NetworkFunctions::IsConnected());

    if(this->client.actionRevision() != this->seen_action_rev){
        this->seen_action_rev = this->client.actionRevision();
        this->onActionDone();
    }
    if(this->client.tasksRevision() != this->seen_tasks_rev){
        this->seen_tasks_rev = this->client.tasksRevision();
        this->refreshList();
        this->refreshDetail();
        this->refreshStatus();
    }
    if(this->client.statusRevision() != this->seen_status_rev){
        this->seen_status_rev = this->client.statusRevision();
        this->refreshStatus();
    }
    if(this->client.tasksRevision() != this->synced_tasks_rev && this->client.loaded()){
        this->synced_tasks_rev = this->client.tasksRevision();
        this->syncReminders();
    }

    //日付が変わる/期限を過ぎると見た目(今日/明日・赤)が変わるので、時々見直す
    const unsigned long now_ms = millis();
    if(now_ms - this->last_clock_check_ms >= 10000){
        this->last_clock_check_ms = now_ms;
        int32_t now_sec = 0;
        const int32_t today = TodayDay(&now_sec);
        if(today != this->shown_today || countOverdue(today, now_sec) != this->shown_overdue){
            this->refreshList();
            this->refreshDetail();
            //時計が合ったら(NTP同期)リマインダーも入れ直す
            if(this->shown_today >= 0) this->syncReminders();
        }
    }
}

void TodoScene::onExit(){
    //onUpdate() が来なくなるので、通信を打ち切って接続(TLSの約40KB)も返す
    this->client.stop();
    this->pending_due_dialog = false;
    this->confirm_id.clear();

    this->back_button = nullptr;
    this->refresh_button = nullptr;
    this->settings_button = nullptr;
    this->title_label = nullptr;
    this->view_tab = nullptr;
    this->task_list = nullptr;
    this->detail_label = nullptr;
    this->status_label = nullptr;
    this->add_button = nullptr;
    this->done_button = nullptr;
}
