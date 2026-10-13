#pragma once

#include "gui/scenes/Scene.hpp"
#include "video/Pvid_Player.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class Button;
class SeekBar;
class VideoView;
class FileSelectDialog;
template<size_t N> class Label;

// 動画(.pvid、VIDEO_FORMAT.md)を再生するアプリ。ランチャの「動画」、ファイルアプリで .pvid を2回タップしても開く。
//
//   [戻る][開く] 題名
//   動画(VideoView。等倍で真ん中)
//   ━━━━●━━━━━  0:12 / 1:30   (SeekBar。ドラッグして離した位置へ飛ぶ)
//   [-10秒][再生/一時停止][+10秒]
//
// ---- 時計 ----
// 同じ名前の .wav があれば SoundFunctions::WavPlay() で鳴らし、WavPositionMs() から出すフレームを決める
// (音と絵がずれない)。.wav が無い/先に終わったら millis() で進める。
// 1回の onUpdate() で解くのは kMaxDecodePerUpdate 枚まで。それより遅れたら、キーフレームから解き直す方が
// 近ければそうする(PvidPlayer::seekTo)。遅れた分の絵は描かずに飛ばすので、重いときは絵がカクつくだけで音は止まらない。
//
// ---- パレット ----
// 再生中は16色全部を動画のものにする(PICO_GFX::COLORS。ステータスバー等の色も変わる)。
// 動画を閉じる・画面を離れると ResetPalette() でテーマの色へ戻す。
//
// ---- 寿命 ----
// 絵のバッファ(幅/2×高さ)と目録は動画を開いている間だけ(PvidPlayer)。画面を離れると閉じ、
// 上へ別の画面が載って戻ってきたら同じ動画をその時刻から開き直す。
class VideoScene : public Scene {
    public:
        constexpr static int kMaxDecodePerUpdate = 4;
        constexpr static uint32_t kSkipMs = 10000;

        explicit VideoScene(const char* path = nullptr);
        const char* getName() const override { return "VideoScene"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        bool onKey(const KeyInputFunctions::Event& ev) override;

    private:
        PvidPlayer player;
        VideoView* view = nullptr;
        Button* back_button = nullptr;
        Button* open_button = nullptr;
        Button* play_button = nullptr;
        Button* rew_button = nullptr;
        Button* fwd_button = nullptr;
        SeekBar* bar = nullptr;
        Label<PICO_STR_M>* title_label = nullptr;
        Label<PICO_STR_S>* time_label = nullptr;
        FileSelectDialog* picker = nullptr;

        FixedString<PICO_PATH_LEN> path;
        FixedString<PICO_PATH_LEN> wav_path;   // 空 = 音なし
        FixedString<PICO_PATH_LEN> pending_path;

        enum class Pending : uint8_t { None, OpenPicker, Load, Cancelled };
        Pending pending = Pending::None;
        uint8_t pending_wait_frames = 0;

        bool playing = false;      // 再生中(一時停止・終わりで偽)
        bool ended = false;
        bool audio_active = false; // 時計を WAV から取っている
        uint32_t clock_ms = 0;     // millis() で進めるときの、resume_millis の時点の動画の時刻
        uint32_t resume_millis = 0;
        uint32_t start_at_ms = 0;  // 開き直すとき(戻ってきたとき)の時刻
        int last_shown_sec = -1;
        bool last_playing_label = false;

        void openPicker();
        bool load(const char* p, uint32_t at_ms);
        void closeVideo();
        uint32_t nowMs() const;
        void setPlaying(bool on);
        void seekMs(uint32_t ms);
        void togglePlay();
        void refreshControls(bool force);
};
