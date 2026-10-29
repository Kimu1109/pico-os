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
    };

    enum class Kind : uint8_t { None = 0, Mml = 1, Wav = 2 };

    // ---- 操作 ----
    // tracks/folders を再生の順番として写し、track から鳴らす(-1なら並びの頭から=ミックスなら混ぜた頭)。
    // playlist は画面側の番号(一覧の色付けに使うだけ)、playlist_name は見せる名前。
    // 状態を確保できなければ false
    bool StartPlaylist(const Track* tracks, int count,
                       const FixedString<PICO_STR_M>* folders, int folder_count,
                       int playlist, const char* playlist_name, int track);
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
    bool IsMml(const char* name);
    // WAVの経路(SoundFunctions::WavPlay)で鳴らすファイル(.wav / .mp3)か
    bool IsWavOrMp3(const char* name);

    // BackgroundFunctions へ登録する口
    BackgroundService* Service();

    // music.cfg(shuffle / repeat)を読み直す
    void LoadSettings();

    namespace Test {
        void Reset();   // 状態を返して初期値へ(ホストテスト用)
    }
}
