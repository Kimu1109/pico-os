#pragma once

#include <cstdint>

class BackgroundService;

// ホーム画面のTODOウィジェット用に、Todoistの「今日」の一覧を定期的に取り直す(バックグラウンドの仕事「TODOの取り直し」)。
//
// - **ホーム画面にTODOウィジェットを置いている間だけ**動く(置いていなければ何もしない)
// - kIntervalMs ごとに TodoistClient を new して「今日」を1回取り、TodoCache::Store() して捨てる
//   (TodoistClient 約20KB + TLS 約40KB は取りに行く数秒の間だけ)
// - TODOアプリが開いている間は取りに行かない(画面が取って TodoCache へ入れるので)
// - Wi-Fiに繋がっていない・時計が合っていない間は10秒後に見直す
// - 入り切りは /sys/background.cfg の todo(既定は入)
namespace TodoWatch {
    constexpr uint32_t kIntervalMs = 15UL * 60UL * 1000UL;
    constexpr uint32_t kRetryMs = 5UL * 60UL * 1000UL;
    constexpr uint32_t kGiveUpMs = 40000;

    BackgroundService* Service();

    // TODOアプリが前に出ているか(取りに行かない)。Background_List.cpp が差す
    extern bool (*foreground_query)();
}
