#pragma once

#include <cstdint>

class BackgroundService;

// アプリストアの更新の見張り(バックグラウンドの仕事「アプリの更新の確認」)。
//
// - kIntervalSec(1日)ごとに AppStore を new して一覧(catalog.tsv と署名)だけを取り直し、
//   入っているアプリに更新があれば「N件のアプリに更新があります」と通知する(押すとストアが開く)。入れ替えはしない
//   (ストアの「すべて更新」か1つずつの[更新]で、利用者が入れる。動いているアプリのファイルを裏で書き換えないため)
// - 知らせるのは AppStore::updateCount() のもの(前の版へ戻して見送っている版・本体が古くて入れられないものは数えない)。
//   同じ顔ぶれ(idと版の組)は2回知らせない(/sys/store/state.cfg の notified)
// - ストアが開いている間は取りに行かない(画面が取るので)。Wi-Fiに繋がっていない間は5分後に見直す
// - 最後に確かめた時刻は /sys/store/state.cfg の checked(UNIX時刻)。時計が合っていない間は起動からの時間で数える
// - AppStore 約27KB + TLS 約40KB は取りに行く数秒の間だけ
// - 入り切りは /sys/background.cfg の store-update(既定は入)
namespace StoreWatch {
    constexpr uint32_t kIntervalSec = 24UL * 60UL * 60UL;
    constexpr uint32_t kRetryMs = 5UL * 60UL * 1000UL;
    constexpr uint32_t kGiveUpMs = 60000;
    // 起動してすぐは取りに行かない(起動直後のWi-Fiの接続・他の仕事と重ねない)
    constexpr uint32_t kStartDelayMs = 2UL * 60UL * 1000UL;

    BackgroundService* Service();

    // ストアが前に出ているか(取りに行かない)。Background_List.cpp が差す
    extern bool (*foreground_query)();

    // 更新の顔ぶれ("<id>@<版>" を並べたもの)の指紋。同じ顔ぶれを2回知らせないために覚える(ホストテスト用に外へ)
    uint32_t Fingerprint(const char* const* ids, const char* const* versions, int n);
}
