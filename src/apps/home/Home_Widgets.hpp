#pragma once

// ホーム画面のウィジェットの種類ごとの描き方(時計・アナログ時計・音楽・予定・TODO)。
// 並びと置き場所は HomeLayout(Home_Layout.hpp)、ここは描き方と操作だけ。
namespace HomeWidgets {
    // /sys/home.cfg を読み、種類ごとの描き方を HomeLayout へ差す。AppFunctions::Setup() より後
    void Setup();
}
