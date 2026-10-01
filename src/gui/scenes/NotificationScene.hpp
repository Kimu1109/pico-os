#pragma once

#include "gui/scenes/Scene.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
class TabBar;
template<size_t N> class Label;

// 通知センター。ステータスバーをタップするか、ランチャの「通知」から開く。
//
// [戻る] 通知                [既読] [全消去]
// [ 履歴 | 予約 ]
// [一覧(ScrollList。1回目で選んで下へ詳しく、2回目で開く/取り消す)]
// 詳しく(時刻・アプリ・タイトル・本文)
// [通常] [音あり]                   [開く]
//
// - 履歴: 届いた通知(新しい順)。未読は青。2回タップ/[開く]で送ったアプリを開く
// - 予約: まだ出ていない予約と、繰り返しの予約。[取り消す]で消せる(どのアプリの予約でも)
// - [通常]⇔[控えめ]: トーストと音を出す ⇔ 印と履歴だけ。[音あり]⇔[音なし]: 通知音。どちらも /sys/notify.cfg へ書く
// - 中身が変わったら(NotificationFunctions::Revision())一覧を作り直す
// - 画面を離れるときに全部既読にする(ここで見たので)
class NotificationScene : public Scene {
    public:
        static constexpr const char* kName = "Notifications";

        const char* getName() const override { return kName; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        enum class Tab : uint8_t { History = 0, Rules };

        void rebuild();
        void refreshDetail();
        void refreshSettingButtons();
        void doAction();
        // 一覧のindex番目に当たる通知の通し番号/予約のid(無ければ0)
        uint32_t keyAt(int index) const;

        Button* back_button = nullptr;
        Button* read_button = nullptr;
        Button* clear_button = nullptr;
        Button* mode_button = nullptr;
        Button* sound_button = nullptr;
        Button* action_button = nullptr;
        TabBar* tab = nullptr;
        ScrollList* list = nullptr;
        Label<PICO_STR_256B>* detail = nullptr;

        // 一覧の並び(履歴は通し番号、予約はid)。選択を作り直しの前後で保つために持つ
        static constexpr int kMaxRows = 16;
        uint32_t keys[kMaxRows] = {};
        int key_count = 0;
        uint32_t selected_key = 0;

        Tab current_tab = Tab::History;
        uint32_t last_revision = 0;
        unsigned long last_detail_ms = 0;

        constexpr static int MARGIN = 6;
};
