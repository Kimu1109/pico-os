#pragma once

#include "apps/calendar/Ical.hpp"

#include <cstdint>

// カレンダーの予定の通知。読み込んだ.icsの時刻つきの予定を、OSの通知(NotificationFunctions)の
// 「決まった日時(At)」の予約として登録する(Todoistのリマインダー=apps/todo/Todo_Remindersと同じ作り)。
// 予約はOSが見張り /sys/notify_rules.tsv に保存されるので、アプリを閉じていても・再起動しても知らせる
// (タップするとカレンダーが開く)。
//
// - 送り主(owner)は kOwner("@calendar")。'@'で始まる送り主はOSの機能のもので、登録簿と突き合わせて
//   消されない(NotificationFunctions::RefreshOwners)。送り主ごとに kMaxRulesPerOwner(4)件までなので、
//   **近い順に4件まで**。tag は "cal:<UIDのハッシュ>:<日>"(繰り返しの回ごとに別)
// - カレンダーアプリは表示中の格子(6週間)ぶんしか読まないので、「読み込んだ範囲(covered_until)」の
//   中で、今の一覧に無い予約だけを消す(消した/動かした予定)。範囲の外の予約は残す
// - 終日の予定は知らせない(時刻が無いため)
// - 設定は /sys/calendar.cfg(無くてよい): reminders = true|false(既定true)、remind-before-min(既定10)
namespace CalendarReminders {

    constexpr const char* kOwner = "@calendar";
    constexpr const char* kTagPrefix = "cal:";

    struct Settings {
        bool enabled = true;
        int before_min = 10;
    };
    // /sys/calendar.cfg を読む(無ければ既定)
    Settings LoadSettings();

    // 予約を読み込んだ予定に合わせる。
    //   from_day/to_day … 読み込んだ範囲(現地の通算日。to_dayは含まない)
    //   now_epoch       … 今(UTC)。これより後に知らせるものだけ登録する
    // 登録している予約の数を返す
    int Sync(const IcalCalendar& cal, int32_t from_day, int32_t to_day, int64_t now_epoch,
             int32_t utc_offset_sec, int before_min, const char* app_name);

    // 全部取り消す(通知を切ったとき)
    void CancelAll();
    int Count();
}
