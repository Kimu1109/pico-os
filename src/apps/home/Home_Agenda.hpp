#pragma once

#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// ホーム画面の「予定」ウィジェットに出す、今日から7日間の予定(SDの /calendar/*.ics から)。
//
// カレンダーアプリ(CalendarScene)と同じ .ics を読むが、IcalCalendar(約16KB)は読む間だけ確保し、
// 見せる分(kMaxEntries 件、1件約70バイト)だけを静的に持つ。読み直すのは
// 「ランチャを開いたときに日付が変わっていた/カレンダーアプリが .ics を取り直したかもしれない(Invalidate)」とき。
// .ics の取得そのものはカレンダーアプリの仕事(ここでは通信しない)。
namespace HomeAgenda {
    constexpr int kMaxEntries = 8;
    constexpr int kDays = 7;

    struct Entry {
        int32_t day = 0;            // 始まる日(続きの予定は今日)
        int32_t start_sec = -1;     // -1 = 終日/前日からの続き
        int32_t end_sec = -1;       // その日の0時からの終わり(day の翌日以降に終わるなら 86400 以上)
        bool continued = false;     // 前の日から続いている
        int8_t color = PICO_DARKGREEN;
        FixedString<PICO_STR_M> title;
    };

    // 今日(today)から kDays 日を読み直す。now_sec より前に終わった今日の予定は入れない
    void Refresh(int32_t today, int32_t now_sec, int32_t utc_offset_sec);
    // 日付が変わっていた・Invalidate() されていたら読み直す
    void RefreshIfStale();
    // .ics が変わったかもしれない(カレンダーアプリを閉じたとき)
    void Invalidate();

    int Count();
    const Entry& At(int i);
    // 読み直すたびに増える
    uint32_t Revision();
    // 読んだ日(読んでいなければ -1)
    int32_t Day();
    // .ics が1つも無かった
    bool NoFiles();
}
