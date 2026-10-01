#pragma once

#include "gui/scenes/Scene.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
class NumberSlider;
template<size_t N> class Label;

// ミュージック: SDの /music/ 直下の *.mml(MUSIC_FORMAT.md)と *.wav を並べて鳴らす。
//
// [戻る] ミュージック
// [曲の一覧(ScrollList。2回タップで鳴らす)]
// 状態の欄(2行。鳴っている曲名 / 案内 / 読めなかった理由)
// 0:23 [シークバー] 3:45        ← 鳴っている間だけ。シークバーはWAVだけ(離したところへ飛ぶ)
// [一時停止/再開] [停止]        ← 鳴っている間だけ
//
// - 時間は MML(曲の長さは読み込み時に数える。L で繰り返す曲は終わりが無いので「ループ」)とWAVの両方に出す
// - 読めない曲は、行・列つきの理由を状態の欄へ赤で出す(鳴っている曲はそのまま。WAVは読み取り係が1つなので止まる)
// - MMLとWAVは同時には鳴らさない(片方を鳴らすともう片方は止める)
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
        // 一時停止/停止ボタン・時間・シークバー(鳴っている間だけ出す)。毎フレーム呼ぶが、変わったときしか書き換えない
        void refreshTransport();
        void togglePause();

        Button* back_button = nullptr;
        Button* stop_button = nullptr;
        Button* pause_button = nullptr;
        Label<PICO_STR_S>* cur_label = nullptr;
        Label<PICO_STR_S>* total_label = nullptr;
        NumberSlider* seek_bar = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        Label<PICO_STR_256B>* status_label = nullptr;
        ScrollList* list = nullptr;

        FixedString<PICO_STR_M> file_names[kMaxFiles];
        int file_count = 0;

        //状態の欄を書き換えるのは変わったときだけ(毎フレーム書くとLabelがdirtyを積み続ける)
        int last_status = -1;
        FixedString<PICO_STR_M> last_title;
        bool last_paused = false;

        //操作部の表示状態(0=非表示 1=MML 2=WAV)と、書き込み済みの値
        int transport_mode = 0;
        bool transport_paused = false;
        int64_t last_cur_sec = -1;
        int64_t last_total_ms = -1;
        int last_bar_px = -1;
        uint32_t bar_duration = 0;
        bool seek_pending = false;      // つまみを動かした(離したときに飛ぶ)
        bool updating_bar = false;      // こちらから値を書いている最中(動かしたことにしない)
        int bar_w = 1;

        constexpr static int MARGIN = 6;
};
