#pragma once

// 電源メニュー(スリープ / 再起動)。
// ステータスバーの長押し(main.cpp)と、設定アプリの上の段の「電源」ボタンから開く。
//
// - スリープ … PowerFunctions::SleepNow()。指を離した次のフレームで入り、タッチ・ボタン・打鍵で起きる
// - 再起動   … 次のフレームの頭(Update())で、今の画面のonExit()(編集中の保存・ゲームボーイの.sav等)・
//              通知の予約の保存・ログの書き出しを済ませてから CrashDumpFunctions::Reboot()
//              (落ちたのではないので、次の起動でクラッシュダンプは書かれない)
namespace PowerMenuFunctions {
    // ステータスバーをこの時間押し続けたら開く
    constexpr unsigned long kLongPressMs = 600;

    // メニューを開く(既に開いていれば何もしない)
    void Open();
    // 再起動を頼む(メニューを通さずに。次のUpdate()で再起動する)
    void RequestReboot();
    // loop()の SceneFunctions::Update() の直後に呼ぶ(ウィジェットのコールバックの外で再起動するため)
    void Update();
}
