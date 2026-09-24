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
// 1つのシーンで「部屋の一覧」と「部屋の中(発言の一覧 + 入力欄)」を切り替える
// (ClocksScene と同じく、表示/非表示は applyMode() の1箇所で決める)。
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
        enum class Mode : uint8_t { List, Room };

        Button* back_button = nullptr;
        Button* refresh_button = nullptr;
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

        // 画面へ反映済みの版(ChatClient の revision と比べて、変わったときだけ描き直す)
        uint32_t seen_rooms_rev = 0;
        uint32_t seen_msgs_rev = 0;
        uint32_t seen_status_rev = 0;
        bool seen_sending = false;

        // 一覧の行 → 部屋のid
        uint32_t list_room_ids[ChatClient::kMaxRooms] = {};
        int list_count = 0;

        // 画面を1回描いてから繋ぎに行く(TLSのハンドシェイクで止まる前に画面を出しておくため)
        int frames_since_enter = 0;

        // 配置(onEnter()で実測して決める)
        int body_top = 0;
        int input_row_y = 0;
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

    public:
        const char* getName() const override { return "Chat"; }

        void onEnter() override;
        void onUpdate() override;
        void onExit() override;
};
