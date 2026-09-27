#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/apps/ChatLogView.hpp"
#include "chat/Chat_Client.hpp"

#include <cstdint>

// チャットアプリ。自前のチャットサーバ(server/chat/、仕様は CHAT_PROTOCOL.md)へ繋ぐ。
//
// 1つのシーンで「部屋の一覧」「オープンチャットの検索」「部屋の中(発言の一覧 + 入力欄)」を切り替える
// (ClocksScene と同じく、表示/非表示は applyMode() の1箇所で決める)。
//
// 一覧に出るのは参加している部屋だけ(オープンチャット = 吹き出し、プライベートチャット = 鍵のアイコン)。
// 一覧の下の [部屋を探す] でオープンチャットを検索して参加し、[コードで参加] で参加コードを打って
// プライベートチャットに入る。プライベートチャットの中では [招待] で参加コード(30分で無効)を出せる。
// 部屋を作る・抜ける・メンバーの管理は Web から行う(CHAT_PROTOCOL.md)。
//
// 接続先は SD の /sys/chat.cfg。Webクライアントの「pico-os の設定」で出る内容をそのまま置く:
//     server = https://chat.example.com
//     token = (43文字)
//
// **このシーンは約25KBある**(ChatClient が発言30件ぶん約17.5KB + 部屋の一覧 + 受信の行バッファ + HttpRequest を持つ)。
// MarkdownScene / CalendarScene と同じく「シーン本体は数十バイト」の例外。さらに**繋いでいる間は
// TLSの約40KBを持ち続ける**(接続を使い回すため)。onExit() で閉じる。
class ChatScene : public Scene {
    private:
        enum class Mode : uint8_t { List, Search, Room };

        Button* back_button = nullptr;
        Button* refresh_button = nullptr;
        Button* invite_button = nullptr;
        Button* settings_button = nullptr;
        Button* search_button = nullptr;
        Button* code_button = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        ScrollList* room_list = nullptr;
        ChatLogView* log_view = nullptr;
        Textbox<PICO_STR_LL>* input = nullptr;
        Button* send_button = nullptr;
        Label<PICO_STR_LL>* status_label = nullptr;

        ChatClient client;

        // onExit() を跨いで残す(上へ別のシーンをPush()して戻ったときに復元する)
        Mode mode = Mode::List;
        FixedString<PICO_STR_LL> draft;
        ChatProto::Name search_query;

        // 利用者の操作の結果など、状態の行へ出す一言(通信の失敗の理由があればそちらが優先)
        FixedString<PICO_STR_LL> notice;
        int8_t notice_color = PICO_DARKGREY;
        // 参加コードを受け取った。次のフレームでダイアログを出す
        bool pending_invite_dialog = false;
        // サーバURLの編集を終えた。次のフレームでトークン編集ダイアログを出す
        // (ダイアログからダイアログは1フレーム空ける。MarkdownScene::Pendingと同じ理由)
        bool pending_token_dialog = false;
        // /sys/chat.cfgのserverの現在値(表示・プレフィル用)。tokenは平文を保持しない
        // (Wi-Fiパスワードと同じ扱いで、編集ダイアログは常に空欄から始まる)
        FixedString<PICO_STR_LL> chat_server_value;

        // 画面へ反映済みの版(ChatClient の revision と比べて、変わったときだけ描き直す)
        uint32_t seen_rooms_rev = 0;
        uint32_t seen_msgs_rev = 0;
        uint32_t seen_status_rev = 0;
        uint32_t seen_action_rev = 0;
        uint32_t seen_room_lost_rev = 0;
        bool seen_sending = false;

        // 一覧の行 → 部屋のid(検索中は検索結果の部屋のid)
        constexpr static int kMaxListRows = (ChatClient::kMaxRooms > ChatClient::kMaxSearchHits)
                                          ? ChatClient::kMaxRooms : ChatClient::kMaxSearchHits;
        uint32_t list_room_ids[kMaxListRows] = {};
        int list_count = 0;

        // 画面を1回描いてから繋ぎに行く(TLSのハンドシェイクで止まる前に画面を出しておくため)
        int frames_since_enter = 0;

        // 配置(onEnter()で実測して決める)
        int body_top = 0;
        int input_row_y = 0;
        int action_row_y = 0;
        int row_h = 0;

        constexpr static int MARGIN = 3;

        void applyMode();
        void openRoom(uint32_t room_id);
        void backToList();
        void refreshRoomList();
        void refreshTitle();
        void refreshStatus();
        void refreshSendButton();
        void refreshPlaceholder();
        void sendDraft();
        void onListTap(int index);
        void openSearchInput();
        void openCodeInput();
        void startSearch();
        void showInvite();
        void loadChatConfigValues();
        void openChatSettings();
        void openTokenDialog();
        void commitChatServer(const FixedString<PICO_STR_LL>& input);
        void commitChatToken(const FixedString<PICO_STR_LL>& input);
        void onActionDone();
        void setNotice(const char* text, int8_t color = PICO_DARKGREY);

    public:
        const char* getName() const override { return "Chat"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
