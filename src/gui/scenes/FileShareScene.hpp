#pragma once

#include "services/FileShare_Functions.hpp"

#include "gui/core/Scene.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class Button;
class QRCodeView;
template<size_t N> class Label;

// Wi-Fi転送の画面(ランチャの「Wi-Fi転送」)。開いている間だけ、同じLANのブラウザからSDのファイルを
// 読み書きできる(中身は FileShareFunctions、設計は docs/design/file-share.md)。
//
//   [戻る] Wi-Fi転送          [直接つなぐ]   ← 本体がAPになる(家のWi-Fiが無い所で)
//   ブラウザで開いてキーを入れる        [QR]   ← キー付きのURL(読めばキーを打たずに開ける)
//   abcd2345
//   QRならキーは不要
//   http://192.168.1.23/                    ← URLは長いので横いっぱいに
//   http://picoos.local/
//   ↑ 45% 1.2MB/2.6MB song.wav              ← 今の転送(割合を先に。長い名前は後ろが切れる)
//   ↑ main.lua(2.1KB)                       ← 最近の操作(入るだけ、4つまで)
//   暗号化しない送り方です。…               ← 下へ寄せる
//
// 直接つなぐの間は、スマホが本体のWi-Fiへ繋ぐまでQRを「Wi-Fiへ繋ぐ」もの(WIFI:…)にし、
// 繋いだらページのもの(キー付きのURL)に替える。下の2行は、繋ぐ前はAPの名前とパスワード、繋いだ後はURLとパスワード。
// 画面の文字には「繋」を使わない(フォントに無い。「つなぐ」と書く)。
// 開いている間は眠らない・別の画面へ移らない(keepForeground)。閉じると待ち受けをやめる。
class FileShareScene : public Scene {
    public:
        static constexpr const char* kAppName = FileShareFunctions::kAppName;

        const char* getName() const override { return "FileShare"; }
        bool keepForeground() const override { return true; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        void refreshAddress(bool force);
        void refreshActivity(bool force);
        void toggleDirect();

        Label<PICO_STR_LL>* url_ip_label = nullptr;
        Label<PICO_STR_LL>* url_name_label = nullptr;
        Label<PICO_STR_M>*  key_label = nullptr;
        Label<PICO_STR_LL>* current_label = nullptr;
        Label<PICO_STR_1KiB>* recent_label = nullptr;
        Label<PICO_STR_LL>* hint_label = nullptr;
        Button* direct_button = nullptr;
        QRCodeView* qr = nullptr;
        int recent_lines = 1;     // 最近の操作を何行見せるか(画面の大きさで決まる)
        bool failed = false;
        bool ap_joined = false;
        uint32_t clients_checked_ms = 0;
        int8_t shown_address = -1;   // 繋がり・直接つなぐ・APに端末が繋いだか(変わったらURL・QRを出し直す)
        uint32_t shown_revision = 0xFFFFFFFFu;
        uint64_t shown_done = UINT64_MAX;
        uint32_t last_refresh_ms = 0;

        constexpr static int MARGIN = 6;
        constexpr static int kQrSize = 99;   // 33升(版4。URL+キー)で1升3px
        constexpr static int kDirectButtonW = 84;
        constexpr static uint32_t kClientsCheckMs = 500;
        constexpr static uint32_t kRefreshMs = 250;
};
