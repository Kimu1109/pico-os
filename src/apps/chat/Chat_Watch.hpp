#pragma once

#include <cstdint>

#include "apps/chat/Chat_Proto.hpp"

class BackgroundService;

// チャットの未読の見張り(バックグラウンドの仕事「チャットの未読」)。
//
// チャットアプリ(ChatScene)を閉じている間も、/sys/chat.cfg のサーバへ数分おきに部屋の一覧
// (GET /api/v1/rooms。未読の数が付いてくる。取るだけでは既読にならない)を取りに行き、
// 新しい発言で未読が増えた部屋があれば通知を出す(タップでチャットアプリが開く)。
//
// - 間隔は chat.cfg の notify-poll-sec(秒。既定120、30以上)。入り切りは /sys/background.cfg の chat
// - チャットアプリを開いている間は取りに行かない(画面の ChatClient が取っているので)
// - **接続は使い回さない**(Connection: close)。TLSの約40KBは取りに行く数秒の間だけ持つ。
//   要求の道具(HttpRequest + 行バッファ + 部屋16件、約4KB)も取りに行く間だけ new する
// - Wi-Fiに繋がっていない・時計が合っていない(HTTPSの証明書を確かめられない)間は待つ
// - 同じ部屋は「最後に知らせた発言id」より新しい発言が来たときだけ知らせる(RAMだけ。再起動後の
//   最初の1回は、未読のある部屋を全部知らせる)
namespace ChatWatch {
    constexpr uint32_t kDefaultIntervalSec = 120;
    constexpr uint32_t kMinIntervalSec = 30;
    constexpr int kMaxRooms = 16;

    BackgroundService* Service();

    // チャットアプリが前に出ているか(取りに行かない)。Background_List.cpp が差す
    extern bool (*foreground_query)();

    // 部屋の一覧を受け取った: 新しく未読が増えた部屋を知らせ、知らせた数を返す(ホストテストの口を兼ねる)
    int OnRooms(const ChatProto::Room* rooms, int count);

    namespace Test {
        void Reset();
    }
}
