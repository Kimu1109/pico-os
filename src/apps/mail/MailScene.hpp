#pragma once

#include "gui/core/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TextView.hpp"
#include "apps/mail/Imap_Client.hpp"

#include <cstdint>

// メールアプリ(受信だけ)。IMAPのサーバから受信箱を読んで見せる。
//
//   一覧                                   読む
//   [戻る] メール (未読 2)    [設定][更新]  [一覧] 件名
//   ✉ 10:05 山田 打ち合わせの件             差出人: 山田 太郎 <taro@…>
//   ✉ 10/20 Amazon ご注文の確認             日時: 2026/10/20 10:05
//     10/18 news 週刊ニュース               宛先: you@…  添付: 1件(請求書.pdf)
//     …                                     本文(TextView。上下のドラッグでスクロール)
//   状態の1行                               状態の1行(長すぎて途中までのとき等)
//
// - 一覧は新しい順に ImapClient::kMaxMessages 通。未読は閉じた封筒のアイコンと黒字、既読は開いた封筒と灰色
// - タップで本文を開く(既読にするかは /sys/mail.cfg の mark-read。既定は既読にする)
// - [設定] でサーバ → ユーザー名 → パスワード の順に聞いて /sys/mail.cfg へ書く(パスワードは暗号化)
// - 開いている間は数分ごとに新着を確かめる(ImapClient)。閉じたら接続(TLSの約40KB)も返す
// - 物理キーボード: Esc で一覧へ/アプリを閉じる、↑↓ PageUp/PageDown で本文をスクロール、r で更新
//
// **このシーンは約30KBある**(ImapClient が一覧 × 2 と本文12KBを持つ)。TodoScene と同じく
// 「シーン本体は数十バイト」の例外。
class MailScene : public Scene {
    public:
        // ランチャの名前(App_List.cpp と揃える)
        static constexpr const char* kAppName = "メール";

    private:
        enum class Mode : uint8_t { List, Read };
        // 設定のダイアログは1つずつ、1フレーム空けて次を出す(MarkdownScene::Pending と同じ理由)
        enum class Pending : uint8_t { None, User, Password };

        // ---- 両方 ----
        Button* back_button = nullptr;
        Label<PICO_STR_LL>* title_label = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;
        // ---- 一覧 ----
        Button* refresh_button = nullptr;
        Button* settings_button = nullptr;
        ScrollList* mail_list = nullptr;
        // ---- 読む ----
        Label<PICO_STR_1KiB>* header_label = nullptr;
        TextView* body_view = nullptr;

        ImapClient client;

        Mode mode = Mode::List;
        uint32_t reading_uid = 0;
        // 本文の欄に出す案内(読み込み中・失敗の理由)。TextView は文字列を指すだけなので値で持つ
        FixedString<PICO_STR_LL> body_message;

        // 一覧の行 → client.messageAt() の添字(並びが同じなら行を作り直さず中身だけ書き換える)
        int list_count = 0;
        uint32_t list_uids[ImapClient::kMaxMessages] = {};

        // 利用者の操作の結果など、状態の行へ出す一言(通信の失敗の理由があればそちらが優先)
        FixedString<PICO_STR_LL> notice;
        int8_t notice_color = PICO_DARKGREY;

        // 設定の入力(ダイアログを跨いで持つ)
        Pending pending = Pending::None;
        FixedString<PICO_STR_M> input_host;
        uint16_t input_port = ImapClient::kDefaultPort;
        FixedString<PICO_STR_L> input_user;

        uint32_t seen_list_rev = 0;
        uint32_t seen_status_rev = 0;
        uint32_t seen_body_rev = 0;
        // 一覧の日付の出し方(今日なら時刻)が日付の変わり目で変わるのを見るため
        int shown_yday = -1;
        unsigned long last_clock_check_ms = 0;

        // 画面を1回描いてから繋ぎに行く(TLSのハンドシェイクで止まる前に画面を出しておくため)
        int frames_since_enter = 0;

        constexpr static int MARGIN = 3;

        void setMode(Mode m);
        void refreshList();
        void refreshTitle();
        void refreshStatus();
        void refreshHeader();
        void refreshBody();
        void openMessage(int row);
        void backToList();
        void openSettings();
        void openUserDialog();
        void openPasswordDialog();
        void setNotice(const char* text, int8_t color = PICO_DARKGREY);
        void scrollBody(int dy);

    public:
        // 一覧の日付の形(今日なら "10:05"、今年なら "10/20"、それより前は "2025/10/20")
        static void FormatListDate(int64_t epoch, char* out, size_t cap);
        // "host" / "host:port" を分ける。ポートが無ければ993。読めなければfalse
        static bool ParseHostPort(const char* text, FixedString<PICO_STR_M>& host, uint16_t& port);

        const char* getName() const override { return "Mail"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
};
