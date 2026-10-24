#pragma once

#include "gui/core/Scene.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
template<size_t N> class Label;

// スマホから繋ぎ先のWi-Fiを送ってもらう画面(AP設定モード。設定アプリのWi-Fiタブの[スマホ])。
//
//   [戻る] スマホから設定
//   1. スマホのWi-Fiで次へ接続
//      picoos-setup            ← APの名前とパスワード(大きめの字)
//      12345678
//   2. ページが自動で開きます。開かなければ:
//      http://192.168.42.1/
//   3. 繋ぎたいWi-Fiの名前とパスワードを送る
//   ───────
//   状態(待っています/受け取りました)
//
// 開いている間だけAPを出す(WifiSetupFunctions)。受け取ったら少し見せてから閉じ、そのネットワークへ繋ぐ。
// [戻る]で閉じるとAPを止めて保存済みのネットワークへの自動接続へ戻る。
class WifiSetupScene : public Scene {
    public:
        const char* getName() const override { return "WifiSetup"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        void refreshStatus();

        Label<PICO_STR_512B>* guide_label = nullptr;
        Label<PICO_STR_M>*    ssid_label = nullptr;
        Label<PICO_STR_S>*    pass_label = nullptr;
        Label<PICO_STR_LL>*   status_label = nullptr;
        int      shown_clients = -1;
        uint8_t  shown_state = 0xFF;
        uint32_t saved_at = 0;     // 受け取った時刻(少し見せてから閉じる)
        bool     failed = false;

        constexpr static int MARGIN = 6;
        constexpr static uint32_t kCloseAfterSavedMs = 2500;
};
