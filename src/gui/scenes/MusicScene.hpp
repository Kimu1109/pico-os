#pragma once
#include "functions/Sound_Functions.hpp"

#include "gui/scenes/Scene.hpp"
#include "sound/Play_Queue.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
class MusicPlayerPanel;
template<size_t N> class Label;

// ミュージック: SDの /music/ の *.mml(MUSIC_FORMAT.md)と *.wav を鳴らす。
//
// プレイリストはファイルで持たず、/music/ の直下のフォルダ1つ = プレイリスト1つ(中の曲を名前順)。
// 直下に置いた曲は「すべての曲」(直下+全フォルダの曲)にだけ並ぶ。
//
//  ライブラリ                         プレイリスト
//  [戻る] ミュージック                [戻る] 旅の曲 (12)      [▶全曲]
//  ♪ すべての曲                       ♪ 01_opening
//  📁 旅の曲                          ♪ 02_field        ← 鳴っている曲は緑
//  📁 ゲーム                           …
//  ──────────────────────────────  (下はどちらの画面でも同じ「再生中」の欄 = MusicPlayerPanel)
//   曲名 / プレイリスト名 · 3/12 / 0:23 ━━●── 3:45 / ⤨ ⏮ ▶ ⏭ 🔁
//
// - 曲を1回タップで鳴らす(キー/コントローラーで選んでいるときは決定で)。鳴らした時点のプレイリストが
//   再生の順番になり、別のプレイリストを見に行っても鳴っている順番はそのまま
// - 順番通り/ミックス(シャッフル)とリピート(しない/全曲/1曲)は /sys/music.cfg に覚える(PlayQueue)
// - 曲が終わったら次の曲へ。MMLの L で繰り返す曲(終わりが無い)は1周したら次へ進む(1曲リピートなら繰り返し続ける)
// - 読めない曲: 選んだ曲なら止まって理由を出し続け、曲の終わりで進んだ先なら少し出して次の曲へ飛ばす
//   (全部読めなければ止まる)。理由は欄に収まらないので、欄の文字をタップすると全文を出す。一覧では赤
// - MMLとWAVは同時には鳴らさない。アプリを閉じたら曲も止める(このアプリの外から止める手段が無いため)
// - 物理キーボード: n=次 p=前 k=再生/一時停止 s=ミックス r=リピート
class MusicScene : public Scene {
    public:
        static constexpr int kMaxTracks = PlayQueue::kMax;     // 1つのプレイリストに並べる曲の数
        static constexpr int kMaxFolders = 16;                  // プレイリスト(フォルダ)の数

        const char* getName() const override { return "Music"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
        // 曲(とくにWAV)を聴く画面なので、設定に関わらず44100Hzで鳴らす(44.1kHzのWAVを間引かない)
        uint32_t preferredSampleRate() const override { return SoundFunctions::kSampleRateHigh; }

    private:
        struct Track {
            FixedString<PICO_STR_M> name;   // ファイル名(拡張子込み)
            int8_t folder = -1;             // folders[] の番号。-1 = /music/ の直下
        };
        static constexpr int kAll = -1;     // 「すべての曲」のプレイリスト番号

        // ---- 一覧 ----
        void scanFolders();
        // playlist の曲を out へ名前順に集める
        int scanTracks(int playlist, Track* out);
        void showLibrary();
        void showPlaylist(int playlist);
        void onListSelect(int index, bool already_selected);
        void refreshHeader();
        // 一覧の中の「鳴っている曲」を緑にする
        void refreshListColors();
        void playlistName(int playlist, FixedString<PICO_STR_M>& out) const;

        // ---- 再生 ----
        // 見ているプレイリストを再生の順番にして track から鳴らす
        void startPlaylist(int track);
        // queue の track を鳴らす。読めなければ、skip_on_error なら理由を少し出して次へ飛ばし
        // (曲の終わりで進んだとき・全曲)、そうでなければ止まって理由を出し続ける(利用者が選んだ曲)
        void playTrack(int track, bool skip_on_error);
        bool tryPlay(int track, FixedString<PICO_STR_256B>& error);
        void advance(bool by_user);
        void previous();
        void togglePlay();
        void stopPlayback();
        bool soundActive() const;
        void onPanelButton(int button);

        // ---- 表示 ----
        void refreshPanel();
        void loadSettings();
        void saveSettings();

        Button* back_button = nullptr;
        Button* play_all_button = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        ScrollList* list = nullptr;
        MusicPlayerPanel* panel = nullptr;

        FixedString<PICO_STR_M> folders[kMaxFolders];
        int folder_count = 0;

        // 見ている一覧(-2 = ライブラリ)
        static constexpr int kLibrary = -2;
        int view = kLibrary;
        Track view_tracks[kMaxTracks];
        int view_count = 0;

        // 再生の順番になっているプレイリスト(鳴らした時点のもの)
        Track queue_tracks[kMaxTracks];
        int queue_playlist = kLibrary;
        PlayQueue queue;

        // 今鳴らしているもの(0=無し 1=MML 2=WAV)。終わったことは「鳴っていたのに鳴らなくなった」で知る
        uint8_t playing_kind = 0;
        bool finished = false;          // 最後まで鳴らし終えた(リピートしない)
        int fail_streak = 0;            // 続けて読めなかった曲の数(全部読めなければ止まる)
        // 読めなかった曲の理由。曲名は error_track、理由(「3行12列: …」)は error_detail。
        // 再生中の欄には収まらないので、欄の文字をタップすると全文をダイアログで出す(showErrorDetail)
        FixedString<PICO_STR_M> error_track;
        FixedString<PICO_STR_256B> error_detail;
        bool error_sticky = false;      // 利用者が選んだ曲が読めずに止まった(次に鳴らせるまで出し続ける)
        uint32_t error_until_ms = 0;    // 飛ばしたときに出しておく期限
        uint64_t failed_mask = 0;       // 再生の順番の中で読めなかった曲(一覧で赤にする)
        bool errorShown() const;
        void showErrorDetail();

        constexpr static int MARGIN = 6;
};
