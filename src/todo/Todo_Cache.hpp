#pragma once

#include <cstdint>

#include "todo/Todoist_Proto.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// ホーム画面の「TODO」ウィジェットに出す、最後に取ったTodoistの「今日まで」のタスク(期限が今日か過ぎたもの)。
//
// 一覧を取ったとき(TODOアプリ・バックグラウンドの取り直し TodoWatch)に Store() し、
// /sys/todo_cache.tsv にも書く(再起動しても前回の一覧を出せるように)。持つのは kMax 件と総数だけ。
//   1行目: "#todo <取った時刻(エポック秒)> <総数>"
//   以降: "<期限の日>\t<期限の秒(時刻なしは -1)>\t<優先度>\t<タスク名>"
namespace TodoCache {
    constexpr int kMax = 6;

    struct Entry {
        Todoist::Due due;           // text は持たない
        uint8_t priority = 1;
        FixedString<PICO_STR_M> content;
    };

    // 取った一覧から今日までのものを覚える。all_due_by_today = その一覧が「今日」の表示(全部が今日まで)
    void Store(const Todoist::Task* tasks, int count, bool all_due_by_today, int32_t today_day, int64_t now_epoch);
    // SD から読む(まだ読んでいなければ)
    void EnsureLoaded();

    bool Has();             // 一度でも取った(読めた)
    int Count();
    int Total();            // 今日までのタスクの総数(Count() より多いことがある)
    const Entry& At(int i);
    int64_t FetchedEpoch();
    uint32_t Revision();

    namespace Test {
        void Reset();
    }
}
