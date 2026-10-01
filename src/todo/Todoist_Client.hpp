#pragma once

#include "todo/Todoist_Proto.hpp"
#include "task/Http_Request.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// Todoist(https://todoist.com)との通信係。TodoScene が値メンバとして持ち、毎フレーム update() を呼ぶ。
// API は v1(https://api.todoist.com/api/v1/)。
//
// - 認証は個人用のAPIトークン(Todoistの 設定 → 連携 → 開発者)を `Authorization: Bearer` で送るだけ。
//   OAuth は使わない。トークンは SD の /sys/todoist.cfg に `PICO_Secret` で暗号化して置く
//   (平文で置かれていたら、読んだときに暗号化して書き直す)
// - **接続は使い回す**(HttpRequest の keep-alive。チャットと同じ)。繋いでいる間はTLSの約40KBを持つ
// - 同時に投げる要求は1本だけ。優先順は「完了 > 追加 > 一覧」
// - 一覧は表示(View)ごとに取る: 今日(today | overdue)/ 7日間(overdue | next 7 days)/ すべて。
//   ページに分かれていれば kMaxTasks 件に達するまで続きを取る。取り終えてから入れ替えるので、
//   途中で失敗しても前の一覧は残る
// - 一覧を開いている間は kRefreshMs ごとに取り直す。失敗したら5秒→10秒→…→2分と間を空ける。
//   401/403(トークン違い)は自動では取り直さない
//
// 応答のJSONは流しながら読む(Todoist::TaskParser)。1件あたり約800バイトの応答を何十件取っても
// RAMに載るのはタスク1件ぶんの読みかけと、拾った値だけ。
//
// **約20KBある**(タスク kMaxTasks 件 × 2(表示中と受信中)+ HttpRequest)。
class TodoistClient : public IHttpSink {
    public:
        constexpr static int kMaxTasks = 30;
        constexpr static uint32_t kRefreshMs = 5 * 60 * 1000;
        constexpr static uint32_t kMinBackoffMs = 5000;
        constexpr static uint32_t kMaxBackoffMs = 120000;
        // 1回の一覧で続きのページを取りに行く回数の上限(壊れたサーバで回り続けないため)
        constexpr static int kMaxPages = 8;
        static constexpr const char* kDefaultApi = "https://api.todoist.com";

        using Filter = FixedString<PICO_STR_L>;

        enum class View : uint8_t { Today = 0, Week, All };

        enum class State : uint8_t {
            NotConfigured, // /sys/todoist.cfg が無い/トークンが無い
            Offline,       // Wi-Fiに繋がっていない
            Ok,            // 最後の問い合わせは成功した
            Error,         // 最後の問い合わせは失敗した(しばらくして取り直す)
            AuthError,     // トークンが違う(取り直さない)
        };

        enum class Action : uint8_t { None, Add, Close };

        TodoistClient() = default;
        TodoistClient(const TodoistClient&) = delete;
        TodoistClient& operator=(const TodoistClient&) = delete;

        // /sys/todoist.cfg を読む(無ければ NotConfigured)。使える設定が揃っていればtrue
        bool loadConfig();
        // 設定を直接与える(ホストテスト用。loadConfig() と同じ検査をする)。api は "https://api.todoist.com" 等
        bool configure(const char* token, const char* api = kDefaultApi);
        // トークンを暗号化して /sys/todoist.cfg へ書く(設定画面から)。成功したらtrue
        static bool SaveToken(const char* token);
        // 時刻つきの期限を通知で知らせるか(todoist.cfg の reminders。既定 true)と、何分前に知らせるか
        bool remindersEnabled() const { return reminders_; }
        int remindBeforeMin() const { return remind_before_min_; }
        // 表示ごとのフィルタを差し替える(todoist.cfg の today-filter / week-filter)。空なら既定
        void setFilters(const char* today, const char* week);

        // 期限の UTC(…Z)を現地時刻へずらす量。取得のたびに使う
        void setUtcOffset(int32_t sec){ utc_offset_ = sec; }
        int32_t utcOffset() const { return utc_offset_; }

        // 表示を変える。変わったら一覧を捨てて取り直す
        void setView(View v);
        View view() const { return view_; }

        // 待たずに今すぐ取り直す(「更新」ボタン。401の後もこれで再開する)
        void refreshNow();

        // タスクを足す。due_string は Todoist の自然言語の日付("明日 15時" "毎週月曜" 等。空なら期限なし)。
        // 前の操作が残っている/長すぎるならfalse
        bool add(const char* content, const char* due_string);
        // タスクを完了にする(繰り返しのタスクは次の回へ進む)
        bool close(const char* task_id);

        bool actionBusy() const { return pending_ != Action::None || kind_ == Kind::Add || kind_ == Kind::Close; }
        uint32_t actionRevision() const { return action_rev_; }
        Action lastAction() const { return last_action_; }
        bool lastActionOk() const { return last_action_ok_; }
        const char* actionMessage() const { return action_msg_.c_str(); }
        // 最後に完了にしたタスクのid(成功したとき)
        const char* lastClosedId() const { return closed_id_.c_str(); }

        // 毎フレーム呼ぶ。network_up=false(Wi-Fiが切れている)の間は何もせず接続も閉じる
        void update(bool network_up = true);
        // 進行中の要求をやめ、持っている接続も閉じる(onExit用)
        void stop();
        // 通信中か(画面をスリープさせない目安)
        bool busy() const { return kind_ != Kind::None; }

        State state() const { return state_; }
        const char* statusText() const { return status_.c_str(); }
        uint32_t statusRevision() const { return status_rev_; }

        // 一覧。取り直すたびに tasksRevision() が増える
        int taskCount() const { return task_count_; }
        const Todoist::Task& taskAt(int i) const { return tasks_[i]; }
        const Todoist::Task* findTask(const char* id) const;
        uint32_t tasksRevision() const { return tasks_rev_; }
        // 今の表示で一度でも取れたか(「読み込み中」を出すかどうか)
        bool loaded() const { return loaded_; }
        // kMaxTasks 件に入りきらなかった
        bool truncated() const { return truncated_; }
        // 一覧を取った表示(取り直し中に表示を変えた場合の見分け)
        View loadedView() const { return loaded_view_; }

        // 次の問い合わせで使い回す接続を持っているか(keep-alive の確認用)
        bool connectionKept() const { return req_.hasIdleConnection(); }

        // IHttpSink
        bool write(const void* data, size_t len) override;

    private:
        enum class Kind : uint8_t { None, List, Add, Close };

        HttpRequest req_;
        Todoist::TaskParser parser_;
        Todoist::ErrorParser err_;
        Kind kind_ = Kind::None;
        bool parse_ok_ = true;

        // ---- 設定 ----
        Url api_;
        FixedString<PICO_STR_L> auth_;   // "Authorization: Bearer ..."
        Filter today_filter_;
        Filter week_filter_;
        bool configured_ = false;
        int32_t utc_offset_ = 0;
        bool reminders_ = true;
        int remind_before_min_ = 0;

        // ---- 状態 ----
        State state_ = State::NotConfigured;
        FixedString<PICO_STR_LL> status_;
        uint32_t status_rev_ = 0;
        View view_ = View::Today;
        uint32_t next_poll_ms_ = 0;
        uint32_t backoff_ms_ = 0;
        bool wait_manual_ = false;

        // ---- 操作 ----
        Action pending_ = Action::None;
        Todoist::Content add_content_;
        FixedString<PICO_STR_L> add_due_;
        Todoist::TaskId close_id_;
        Todoist::TaskId closed_id_;
        // 送り終わるまで HttpRequest が指しているので値で持つ
        FixedString<PICO_STR_1KiB> body_;
        Action last_action_ = Action::None;
        bool last_action_ok_ = false;
        FixedString<PICO_STR_LL> action_msg_;
        uint32_t action_rev_ = 0;

        // ---- 一覧 ----
        Todoist::Task tasks_[kMaxTasks];
        int task_count_ = 0;
        uint32_t tasks_rev_ = 0;
        bool loaded_ = false;
        bool truncated_ = false;
        View loaded_view_ = View::Today;

        // 受信中の一覧(最後のページまで取れたら tasks_ と入れ替える)
        Todoist::Task new_tasks_[kMaxTasks];
        int new_count_ = 0;
        bool new_truncated_ = false;
        View req_view_ = View::Today;
        int page_ = 0;
        Todoist::Cursor cursor_;

        bool begin(Kind kind, HttpRequest::Method method, const char* path, const char* query,
                   const void* body = nullptr, size_t body_len = 0);
        bool beginList();
        void beginAction();
        void finish();
        void finishList();
        void finishAction(Kind kind, bool ok, const char* why);
        void setStatus(State s, const char* text);
        void failLater(const char* why);
        void removeTask(const char* id);
        const Filter& filterFor(View v) const;
};
