#pragma once

#include <cstdint>

#include "apps/music/Play_Queue.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class BackgroundService;

// ミュージックアプリの再生(順番・曲の終わりで次へ・読めない曲を飛ばす)を、画面の外で持つ係。
// 以前は MusicScene のメンバで、アプリを閉じると曲も止まっていた。今は OS 常駐で、
// BackgroundFunctions に「ミュージック」として登録され、毎フレーム Update() が呼ばれる。
// ミュージックアプリ(MusicScene)はこれを読んで見せ、ボタンで操作するだけの画面になった。
//
// - 状態(再生の順番の曲64件・フォルダ名・PlayQueue、約4.2KB)は最初に鳴らしたときに malloc し、
//   [停止](バックグラウンドの一覧)か Release() で返す。止めただけ(Stop())では返さない(「続きから」のため)
// - 他のアプリが曲/WAVを鳴らした・出力の周波数が切り替わったら(SoundFunctions::PlaybackSerial() が変わったら)、
//   自分の曲は差し替えられたので止まった扱いにする(次の曲へは進まない)
// - 鳴っている間は 44100Hz を望む(画面が何も望まなければ main.cpp がこちらを使う)
// - SD・描画には触るが、2コア目からは呼ばないこと
namespace MusicService {
    static constexpr int kMaxTracks = PlayQueue::kMax;
    static constexpr int kMaxFolders = 16;

    struct Track {
        FixedString<PICO_STR_M> name;   // ファイル名(拡張子込み)
        int8_t folder = -1;             // フォルダの番号(StartPlaylist() に渡した folders の添字)。-1 = /music/ の直下
        FixedString<PICO_STR_M> title;  // タグの曲名(索引から。無ければ空で、見せるときはファイル名)
    };

    enum class Kind : uint8_t { None = 0, Mml = 1, Wav = 2 };

    // StartPlaylist() の playlist: 0以上 = /music/ のフォルダの番号、それ以外は下の種類
    // (playlist_key は、フォルダなら名前、アーティスト/アルバムならタグの名前(不明なら空))
    static constexpr int kPlaylistAll = -1;
    static constexpr int kPlaylistArtist = -5;
    static constexpr int kPlaylistAlbum = -6;

    // ---- 操作 ----
    // tracks/folders を再生の順番として写し、track から鳴らす(-1なら並びの頭から=ミックスなら混ぜた頭)。
    // playlist は画面側の番号(一覧の色付けに使うだけ)、playlist_name は見せる名前。
    // 状態を確保できなければ false
    // playlist_key は「続きから」で同じ並びを作り直すための名前(nullptr なら playlist_name と同じ)
    bool StartPlaylist(const Track* tracks, int count,
                       const FixedString<PICO_STR_M>* folders, int folder_count,
                       int playlist, const char* playlist_name, int track,
                       const char* playlist_key = nullptr);
    // 曲の番号(順番の中の)を、利用者が選んだ曲として鳴らす(読めなければ止まって理由を出し続ける)
    void PlayTrack(int track);
    void Next();
    void Prev();
    // 鳴っていれば一時停止/再開。止まっていれば続き(最後まで行っていたら頭)から
    void TogglePlay();
    void Stop();
    void SeekMs(uint32_t ms);
    void SetShuffle(bool on);
    void CycleRepeat();
    // 状態を返す(止めてから)。もう一度鳴らすには StartPlaylist() から
    void Release();

    // ---- スリープタイマー(docs/design/sound.md「スリープタイマー」) ----
    // Timer: minutes 分たったら止める(最後の kSleepFadeMs はWAV/MP3の音を小さくしていく)。
    // TrackEnd: 今の曲が終わったら次へ進まずに止める。
    // 止まる・Stop()・Release() で Off に戻る。一時停止の間も時間は進む。状態(st)とは別に持つ(確保しない)
    enum class SleepMode : uint8_t { Off, Timer, TrackEnd };
    static constexpr uint32_t kSleepFadeMs = 8000;
    void SetSleepTimer(uint32_t minutes);   // 0 で Off
    void SetSleepAtTrackEnd();
    void ClearSleepTimer();
    SleepMode SleepTimerMode();
    uint32_t SleepRemainingMs();            // Timer のときだけ(他は0)

    // 毎フレーム(BackgroundFunctions 経由)。曲の終わりで次へ
    void Update();

    // ---- 読む ----
    bool HasQueue();                // 再生の順番がある
    bool Active();                  // 鳴っている(一時停止を含む)
    bool Paused();
    bool Finished();                // 最後まで鳴らし終えた(リピートしない)
    Kind PlayingKind();
    int Count();                    // 順番の曲の数
    int Current();                  // 今の曲(順番の中の番号)。無ければ-1
    int Position();                 // 並びの中の位置(「3/12曲目」)
    const Track* TrackAt(int i);
    int Playlist();                 // StartPlaylist() に渡した playlist(無ければ INT32_MIN)
    const char* PlaylistName();
    const char* PlaylistKey();      // StartPlaylist() の playlist_key
    bool Shuffle();
    PlayQueue::Repeat Repeat();
    uint64_t FailedMask();          // 順番の中で読めなかった曲
    // 読めなかった理由。shown が真の間だけ見せる(sticky = 選んだ曲が読めずに止まった)
    bool ErrorShown();
    bool ErrorSticky();
    const char* ErrorTrack();
    const char* ErrorDetail();
    // 他のアプリに音を取られて止まった(次に鳴らすまで)
    bool Interrupted();
    // 曲・状態が変わるたびに増える(画面が一覧の色を塗り直す合図)
    uint32_t Revision();

    // 曲の見せる名前(拡張子を落とす)
    void DisplayName(const char* file, FixedString<PICO_PATH_LEN>& out);
    // 曲の見せる名前(タグの曲名があればそれ、無ければファイル名から拡張子を落としたもの)
    void TrackTitle(const Track& t, FixedString<PICO_PATH_LEN>& out);
    // アーティスト/アルバムの見せる名前(タグが無い曲の集まりは「不明なアーティスト」等)
    void GroupDisplayName(int playlist, const char* key, FixedString<PICO_STR_M>& out);

    // ---- 続きから(電源を切っても前回の曲の続きから。docs/design/sound.md「続きから鳴らす」) ----
    // 鳴らしている間(約10秒ごと)・一時停止・止める・曲が変わったときに /sys/music_resume.cfg へ、
    // プレイリストの種類と名前・曲(フォルダ名とファイル名)・位置を書く。最後まで鳴らし終えたら消す。
    // 順番(再生の順番)が無いとき(起動した後)の TogglePlay() は、これを読んで同じ並びを作り直し、その曲の位置から鳴らす
    // (WAV/MP3は位置へ飛ぶ。MMLは曲の頭から)。並びはフォルダ/索引から作り直すので、ミックスの並びは混ぜ直しになる
    bool HasResume();
    const char* ResumeTitle();
    uint32_t ResumePositionMs();
    // 続きから鳴らす。並びを作れなければ(フォルダ・曲が無くなった)false
    bool Resume();
    bool IsMml(const char* name);
    // WAVの経路(SoundFunctions::WavPlay)で鳴らすファイル(.wav / .mp3 / .flac)か
    bool IsPcmFile(const char* name);

    // BackgroundFunctions へ登録する口
    BackgroundService* Service();

    // music.cfg(shuffle / repeat)を読み直す
    void LoadSettings();

    namespace Test {
        void Reset();   // 状態を返して初期値へ(ホストテスト用)
    }
}
