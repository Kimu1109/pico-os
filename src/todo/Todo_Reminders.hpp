#pragma once

#include "todo/Todoist_Proto.hpp"

#include <cstdint>

// TODOアプリのリマインダー。時刻つきの期限があるタスクを、OSの通知(NotificationFunctions)の
// 「決まった日時(At)」の予約として登録する。予約はOSが見張り /sys/notify_rules.tsv に保存されるので、
// **アプリを閉じていても、再起動しても期限の時刻に知らせる**(タップするとTODOアプリが開く)。
//
// - 送り主(owner)は空(C++のアプリ)で、tag を "td:<タスクのid>" にして見分ける。
//   空の送り主の予約は kMaxRulesPerOwner(4)件までなので、**近い順に4件まで**しか登録しない
// - 一覧は表示(今日/7日間/すべて)ごとにしか取らないので、取った一覧に**載らなかった**予約を
//   全部消すと、別の表示で入れた予約まで消えてしまう。そこで「この一覧がどこまでの期限を
//   網羅しているか」(covered_until_epoch)を受け取り、その範囲の中で一覧に無い予約だけを消す
//   (完了した/期限を変えた/消したタスク)。範囲の外の予約は残す
namespace TodoReminders {

    constexpr const char* kTagPrefix = "td:";

    // 予約を一覧に合わせる。
    //   now_epoch           … 今(UTCのエポック秒)。これより後に知らせるものだけ登録する
    //   covered_until_epoch … 一覧が網羅している期限の終わり(INT64_MAX = 全部)。一覧が溢れていたら now_epoch
    //   before_min          … 期限の何分前に知らせるか
    //   app_name            … タップで開くアプリ(登録簿の名前)
    // 登録している予約の数を返す
    int Sync(const Todoist::Task* tasks, int count, int64_t now_epoch, int64_t covered_until_epoch,
             int32_t utc_offset_sec, int before_min, const char* app_name);

    // 1件取り消す(完了にしたとき)
    void Cancel(const char* task_id);
    // 全部取り消す(リマインダーを切ったとき)
    void CancelAll();
    // 今登録している数
    int Count();
}
