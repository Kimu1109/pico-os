#pragma once
#include "hal/Sound_Functions.hpp"

#include "gui/core/Scene.hpp"
#include "apps/music/Music_Service.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Button;
class ScrollList;
class MusicPlayerPanel;
template<size_t N> class Label;

// ミュージック: SDの /music/ の *.mml(docs/spec/MUSIC_FORMAT.md)と *.wav を鳴らす。
//
// プレイリストはファイルで持たず、/music/ の直下のフォルダ1つ = プレイリスト1つ(中の曲を名前順)。
// 直下に置いた曲は「すべての曲」(直下+全フォルダの曲)にだけ並ぶ。
//
//  ライブラリ                         プレイリスト
//  [戻る] ミュージック        [⌛]     [戻る] 旅の曲 (12)  [⌛][▶全曲]
//  ♪ すべての曲                       ♪ 01_opening
//  👤 アーティスト                     ♪ 02_field        ← 鳴っている曲は緑
//  💿 アルバム                         …
//  📁 旅の曲
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
// - MMLとWAVは同時には鳴らさない
// - **再生そのものは MusicService(OS常駐)が持つ**ので、アプリを閉じても曲は鳴り続け、終われば次の曲へ進む。
//   止めるのはこの画面か、設定アプリの「バックグラウンド」の一覧(ステータスバーの音符の印が動いている合図)。
//   この画面は MusicService を読んで見せ、ボタンで操作するだけ
// - [⌛] = スリープタイマー(オフ・15〜90分・この曲の終わりで。MusicService)。動いている間は残りの分か「曲末」を出す
// - [アーティスト]/[アルバム] = 索引(MusicLibrary、/sys/music_index.bin)のタグで分けた一覧 → その曲。
//   索引はこの画面を開いている間に少しずつ作り直す(変わった曲だけタグを読む)。曲名はタグがあればタグの曲名
// - 順番が無いとき(起動した後)は、欄に前回の曲(MusicService の「続きから」)を出し、再生で続きから鳴らす
// - 物理キーボード: n=次 p=前 k=再生/一時停止 s=ミックス r=リピート
class MusicScene : public Scene {
    public:
        static constexpr int kMaxTracks = MusicService::kMaxTracks;     // 1つのプレイリストに並べる曲の数
        static constexpr int kMaxFolders = MusicService::kMaxFolders;   // プレイリスト(フォルダ)の数

        const char* getName() const override { return "Music"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;
        // 曲(とくにWAV)を聴く画面なので、設定に関わらず44100Hzで鳴らす(44.1kHzのWAVを間引かない)
        uint32_t preferredSampleRate() const override { return SoundFunctions::kSampleRateHigh; }

    private:
        using Track = MusicService::Track;
        static constexpr int kAll = MusicService::kPlaylistAll;     // 「すべての曲」のプレイリスト番号
        static constexpr int kArtists = -3;                         // アーティストの一覧
        static constexpr int kAlbums = -4;                          // アルバムの一覧
        static constexpr int kArtistTracks = MusicService::kPlaylistArtist;
        static constexpr int kAlbumTracks = MusicService::kPlaylistAlbum;
        static constexpr int kLibraryFixedRows = 3;                 // ライブラリの頭の行(すべて・アーティスト・アルバム)

        // ---- 一覧 ----
        void scanFolders();
        void showLibrary();
        void showPlaylist(int playlist);
        // アーティスト/アルバムの一覧(view = kArtists / kAlbums)
        void showGroups(int view);
        // そのアーティスト/アルバムの曲(playlist = MusicService::kPlaylistArtist / kPlaylistAlbum)
        void showGroupTracks(int playlist, const char* key);
        // 今の一覧を作り直す(索引が差し替わったとき)
        void reloadView();
        void onListSelect(int index, bool already_selected);
        void refreshHeader();
        // 一覧の中の「鳴っている曲」を緑にする
        void refreshListColors();
        void playlistName(int playlist, FixedString<PICO_STR_M>& out) const;
        // 見ているプレイリストを再生の順番にして track から鳴らす(-1 = 全曲)
        void startPlaylist(int track);
        void onPanelButton(int button);
        void showErrorDetail();
        void showSleepPicker();
        // スリープタイマーのボタンの文字(残りの分)を、変わったときだけ出し直す
        void refreshSleepButton();
        // 見ているプレイリストが鳴っている順番と同じか(フォルダの並びが変わっていれば別物)
        bool viewIsQueue() const;

        // ---- 表示 ----
        void refreshPanel();

        Button* back_button = nullptr;
        Button* play_all_button = nullptr;
        Button* sleep_button = nullptr;
        int shown_sleep = -2;       // ボタンに出している値(0=オフ、-1=曲末、それ以外は分)
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
        uint32_t seen_revision = 0;
        // アーティスト/アルバムの名前(一覧のとき)と、曲を見ているアーティスト/アルバムの名前
        FixedString<PICO_STR_M> groups[kMaxTracks];
        int group_count = 0;
        FixedString<PICO_STR_M> view_group;
        uint32_t seen_index = 0;

        constexpr static int MARGIN = 6;
};
