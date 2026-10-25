#pragma once

#include "config/Hardware_Config.hpp"

// ビルド時に選べる内蔵アプリ(1=載せる・既定、0=載せない)。ハードウェア機能(Hardware_Config.hpp)と同じ形で、
// platformio.ini の env の build_flags で -DPICO_APP_GB=0 等を足す(例は env:rpipico2w_lite)。
// PCビルドは cmake -DPICOOS_APP_GB=OFF 等(pc/CMakeLists.txt)。
//
// 載せないアプリは、入口(アプリの一覧・バックグラウンドの仕事・ホーム画面のウィジェット・ファイルからの関連付け)を
// `#if` で外すだけにしてある。中身(apps/<機能>/)はそのままビルドされ、どこからも呼ばれなくなった分を
// リンカ(--gc-sections)が捨てる。ほかのアプリと共有している下回り(Ical・Mail_Charset・Ssh_Sha256 等)は、
// 使う側が残っていれば残る。
//
// 選べるのは apps/<機能>/ のアプリだけ。OSとして要るもの(設定・ファイル・時計・通知・USBドライブ・
// 本体の更新・ホーム)は選べない(本体の更新はファームを戻す道なので外さない)。
//
// この値を `#if` で読むファイルは、必ずこのヘッダを自分で include すること
// (未定義のマクロは `#if` で黙って0になり、アプリが消えたことに気づけない。script/check_layers.py が見張る)。

// Markdown(ブラウザ)
#ifndef PICO_APP_BROWSER
    #define PICO_APP_BROWSER 1
#endif
// 電卓
#ifndef PICO_APP_CALCULATOR
    #define PICO_APP_CALCULATOR 1
#endif
// カレンダー。0 にするとホーム画面の「予定」ウィジェットも外す
#ifndef PICO_APP_CALENDAR
    #define PICO_APP_CALENDAR 1
#endif
// チャット。0 にすると未読を見るバックグラウンドの仕事も外す
#ifndef PICO_APP_CHAT
    #define PICO_APP_CHAT 1
#endif
// 辞書
#ifndef PICO_APP_DICT
    #define PICO_APP_DICT 1
#endif
// ゲームボーイ
#ifndef PICO_APP_GB
    #define PICO_APP_GB 1
#endif
// メール
#ifndef PICO_APP_MAIL
    #define PICO_APP_MAIL 1
#endif
// ミュージック。音声出力(PICO_HW_SOUND)が無ければ既定で外す。
// 0 にするとバックグラウンドの再生とホーム画面の「音楽」ウィジェットも外す
#ifndef PICO_APP_MUSIC
    #define PICO_APP_MUSIC PICO_HW_SOUND
#endif
// メモ。開くとテキストエディタを使うので、テキスト(PICO_APP_TEXTEDITOR)を外してもエディタの中身は残る
#ifndef PICO_APP_NOTES
    #define PICO_APP_NOTES 1
#endif
// 読書。0 にするとファイルビューワーの「読む」と Shift_JIS のテキストの引き渡しも外す
#ifndef PICO_APP_READER
    #define PICO_APP_READER 1
#endif
// SSH
#ifndef PICO_APP_SSH
    #define PICO_APP_SSH 1
#endif
// アプリストア(Luaアプリは外しても /lua/apps/ に置けば動く)
#ifndef PICO_APP_STORE
    #define PICO_APP_STORE 1
#endif
// テキスト(エディタ)。0 にするとファイルビューワーの「編集」も外す
#ifndef PICO_APP_TEXTEDITOR
    #define PICO_APP_TEXTEDITOR 1
#endif
// TODO。0 にするとホーム画面の「TODO」ウィジェットとその取り直しの仕事も外す
#ifndef PICO_APP_TODO
    #define PICO_APP_TODO 1
#endif
// 動画。0 にすると .pvid はファイルビューワーで開く(中身は見えない)
#ifndef PICO_APP_VIDEO
    #define PICO_APP_VIDEO 1
#endif

static_assert(PICO_APP_BROWSER == 0 || PICO_APP_BROWSER == 1, "PICO_APP_BROWSER は 0 か 1");
static_assert(PICO_APP_CALCULATOR == 0 || PICO_APP_CALCULATOR == 1, "PICO_APP_CALCULATOR は 0 か 1");
static_assert(PICO_APP_CALENDAR == 0 || PICO_APP_CALENDAR == 1, "PICO_APP_CALENDAR は 0 か 1");
static_assert(PICO_APP_CHAT == 0 || PICO_APP_CHAT == 1, "PICO_APP_CHAT は 0 か 1");
static_assert(PICO_APP_DICT == 0 || PICO_APP_DICT == 1, "PICO_APP_DICT は 0 か 1");
static_assert(PICO_APP_GB == 0 || PICO_APP_GB == 1, "PICO_APP_GB は 0 か 1");
static_assert(PICO_APP_MAIL == 0 || PICO_APP_MAIL == 1, "PICO_APP_MAIL は 0 か 1");
static_assert(PICO_APP_MUSIC == 0 || PICO_APP_MUSIC == 1, "PICO_APP_MUSIC は 0 か 1");
static_assert(PICO_APP_NOTES == 0 || PICO_APP_NOTES == 1, "PICO_APP_NOTES は 0 か 1");
static_assert(PICO_APP_READER == 0 || PICO_APP_READER == 1, "PICO_APP_READER は 0 か 1");
static_assert(PICO_APP_SSH == 0 || PICO_APP_SSH == 1, "PICO_APP_SSH は 0 か 1");
static_assert(PICO_APP_STORE == 0 || PICO_APP_STORE == 1, "PICO_APP_STORE は 0 か 1");
static_assert(PICO_APP_TEXTEDITOR == 0 || PICO_APP_TEXTEDITOR == 1, "PICO_APP_TEXTEDITOR は 0 か 1");
static_assert(PICO_APP_TODO == 0 || PICO_APP_TODO == 1, "PICO_APP_TODO は 0 か 1");
static_assert(PICO_APP_VIDEO == 0 || PICO_APP_VIDEO == 1, "PICO_APP_VIDEO は 0 か 1");
static_assert(!PICO_APP_MUSIC || PICO_HW_SOUND, "ミュージック(PICO_APP_MUSIC)は音声出力(PICO_HW_SOUND)が要る");
