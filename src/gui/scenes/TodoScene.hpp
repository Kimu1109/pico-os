#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "todo/Todoist_Client.hpp"

#include <cstdint>

// TODOアプリ。Todoist(https://todoist.com)のタスクを見て、足して、完了にする。
//
//   [戻る] TODO              [設定][更新]
//   [ 今日 | 7日間 | すべて ]              ← 表示(TabBar)
//   □ 今日 15:00 牛乳を買う               ← 一覧(ScrollList)。期限切れは赤
//   □ 明日 資料を送る
//   選んだタスクの全文・期限・優先度(3行)
//   状態の1行(失敗の理由など)
//   [追加]            [完了]
//
// - 1回のタップで選び、もう一度タップ(または[完了])で完了の確認を出す
// - [追加] は名前 →(1フレーム空けて)期限の順に聞く。期限は Todoist の自然言語("明日 15時" "毎週月曜")
// - [設定] で APIトークン(Todoistの 設定 → 連携 → 開発者)を入れる。/sys/todoist.cfg に暗号化して保存する
// - 時刻つきの期限は OSの通知として予約する(TodoReminders。アプリを閉じていても期限の時刻に知らせる)
//
// **このシーンは約20KBある**(TodoistClient がタスク30件 × 2 を持つ)。ChatScene と同じく
// 「シーン本体は数十バイト」の例外。さらに**繋いでいる間はTLSの約40KBを持ち続ける**(onExit() で閉じる)。
class TodoScene : public Scene {
    public:
        // ランチャの名前(通知をタップしたときにこの名前で開く。App_List.cpp と揃える)
        static constexpr const char* kAppName = "TODO";

    private:
        Button* back_button = nullptr;
        Button* refresh_button = nullptr;
        Button* settings_button = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        TabBar* view_tab = nullptr;
        ScrollList* task_list = nullptr;
        Label<PICO_STR_512B>* detail_label = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;
        Button* add_button = nullptr;
        Button* done_button = nullptr;

        TodoistClient client;

        // onExit() を跨いで残す(上へ別のシーンをPush()して戻ったときに復元する)
        TodoistClient::View view = TodoistClient::View::Today;
        Todoist::TaskId selected_id;

        // 一覧の行 → タスクの添字(client.taskAt())
        int list_count = 0;
        int list_task_index[TodoistClient::kMaxTasks] = {};

        // 利用者の操作の結果など、状態の行へ出す一言(通信の失敗の理由があればそちらが優先)
        FixedString<PICO_STR_LL> notice;
        int8_t notice_color = PICO_DARKGREY;

        // 追加: 名前を入れた。次のフレームで期限のダイアログを出す
        // (ダイアログからダイアログは1フレーム空ける。MarkdownScene::Pending と同じ理由)
        bool pending_due_dialog = false;
        Todoist::Content pending_content;
        // 完了の確認を出している相手(確認中に一覧が変わっても別のタスクを完了にしないよう id で覚える)
        Todoist::TaskId confirm_id;

        // 画面へ反映済みの版
        uint32_t seen_tasks_rev = 0;
        uint32_t seen_status_rev = 0;
        uint32_t seen_action_rev = 0;
        // 一覧の見た目が時刻で変わる(日付が変わる/期限を過ぎる)のを見るため
        int32_t shown_today = -2;
        int shown_overdue = -1;
        unsigned long last_clock_check_ms = 0;
        // リマインダーを最後に合わせた一覧の版
        uint32_t synced_tasks_rev = 0;

        // 画面を1回描いてから繋ぎに行く(TLSのハンドシェイクで止まる前に画面を出しておくため)
        int frames_since_enter = 0;

        constexpr static int MARGIN = 3;

        static int32_t LocalUtcOffsetSec();
        // 今日(1970-01-01からの通算日数、現地)と0時からの秒。時計が合っていなければ -1
        static int32_t TodayDay(int32_t* now_sec = nullptr);

        void refreshList();
        void refreshDetail();
        void refreshStatus();
        void refreshTitle();
        int countOverdue(int32_t today, int32_t now_sec) const;
        void syncReminders();
        const Todoist::Task* selectedTask() const;
        void onListTap(int index, bool already_selected);
        void confirmClose();
        void openAddDialog();
        void openDueDialog();
        void openSettings();
        void onActionDone();
        void setNotice(const char* text, int8_t color = PICO_DARKGREY);

    public:
        const char* getName() const override { return "Todo"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
