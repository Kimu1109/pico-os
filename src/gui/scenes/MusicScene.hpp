#pragma once

#include "gui/scenes/Scene.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
template<size_t N> class Label;

// ミュージック: SDの /music/ 直下の *.mml(MUSIC_FORMAT.md)を並べて鳴らす。
//
// [戻る] ミュージック
// [曲の一覧(ScrollList。2回タップで鳴らす)]
// 状態の欄(2行。鳴っている曲名 / 案内 / 読めなかった理由)
// [停止]
//
// - 読めない曲は、行・列つきの理由を状態の欄へ赤で出す(鳴っている曲はそのまま)
// - アプリを閉じたら曲も止める(鳴らしっぱなしにすると、止める手段がこのアプリを開き直すしか無いため)
// - 並べるのは名前順に kMaxFiles 件まで(SdFatの列挙順は作った順なので並べ替える)
class MusicScene : public Scene {
    public:
        static constexpr int kMaxFiles = 32;

        const char* getName() const override { return "Music"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

    private:
        void reloadList();
        void playIndex(int index);
        void refreshStatus();

        Button* back_button = nullptr;
        Button* stop_button = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        Label<PICO_STR_256B>* status_label = nullptr;
        ScrollList* list = nullptr;

        FixedString<PICO_STR_M> file_names[kMaxFiles];
        int file_count = 0;

        //状態の欄を書き換えるのは変わったときだけ(毎フレーム書くとLabelがdirtyを積み続ける)
        int last_status = -1;
        FixedString<PICO_STR_M> last_title;

        constexpr static int MARGIN = 6;
};
