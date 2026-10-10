#pragma once

#include "gui/widgets/Widget.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <functional>

// ミュージックアプリの下に置く「再生中」の欄。
//
//  ─────────────────────────────
//   曲名
//   プレイリスト名 · 3/12        (読めなかった理由は赤で2行まで)
//   0:23 ━━━━━━●──────── 3:45
//    ⤨     ⏮     (▶)     ⏭     🔁
//
// 子を持たずrender()で直接描き、タップ位置から逆算する型(AppGrid / TabBar / DurationPicker と同じ)。
// 再生の状態は持たず、シーンが set*() で流し込む(AnalogClockと同じ。SoundFunctionsへ依存しない)。
//
// - シークバーはWAVのときだけつまみが出て、押した/動かした位置を描き、離したときに1回だけ on_seek を呼ぶ
//   (動かすたびにSDを読み直さないため)
// - 時間は秒が変わったときだけ、その行だけを描き直す(setTime()は毎フレーム呼んでよい)
// - キー/コントローラー: 上下でシークバー⇔ボタンの行、左右でボタンを選ぶ(シークバーでは10秒戻る/進む)、決定で押す
class MusicPlayerPanel : public Widget {
    public:
        enum Button : uint8_t { kShuffle, kPrev, kPlay, kNext, kRepeat, kButtons };
        static constexpr int kHeight = 118;

        MusicPlayerPanel(int x, int y, int w){
            this->l_rect = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)kHeight};
        }

        // 曲名と下の行(プレイリスト名・案内・理由)。sub_color はパレット番号
        void setTrack(const char* title, const char* sub, int8_t sub_color);
        // active=曲が鳴っている(一時停止中を含む)
        void setPlaying(bool active, bool paused);
        // repeat: 0=しない 1=全曲 2=1曲
        void setModes(bool shuffle, uint8_t repeat);
        // seekable=シークバーで飛べる(WAV)。loops=終わりの無い曲(MMLのL)。毎フレーム呼んでよい
        void setTime(uint32_t pos_ms, uint32_t total_ms, bool loops, bool seekable);
        // 次/前のボタンを押せるか(プレイリストが空なら押せない)
        void setHasTracks(bool has);

        void setOnButton(std::function<void(int button)> cb){ on_button = cb; }
        void setOnSeek(std::function<void(uint32_t ms)> cb){ on_seek = cb; }
        // 曲名と下の行(文字の所)をタップしたとき。収まらない理由の全文を出すのに使う
        void setOnTextTap(std::function<void()> cb){ on_text_tap = cb; }

        bool isSeeking() const { return seeking; }

        WidgetType getWidgetType() const override { return WidgetType::MusicPlayerPanel; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        void render() override;
        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;

        bool focusableByDefault() const override { return true; }
        bool drawsOwnFocus() const override { return true; }
        bool onFocusKey(FocusKey key) override;
        Rect focusRect() const override;

        // 当たり判定とrender()が同じ位置を使う(テストからも見る)
        Rect buttonRect(int index) const;    // パネル内の座標
        Rect barRect() const;                // シークバーの線(パネル内の座標)

    private:
        void drawGlyph(int index, int cx, int cy, int8_t color, int8_t bg);
        void markRow(int y, int h);
        int buttonAt(int lx, int ly) const;
        uint32_t msAt(int lx) const;
        // 今のバーの位置(px、barRect().x から)
        int knobPx() const;

        FixedString<PICO_STR_M> title;
        FixedString<PICO_STR_256B> sub;
        int8_t sub_color = PICO_DARKGREY;

        bool active = false;
        bool paused = false;
        bool shuffle = false;
        uint8_t repeat = 0;
        bool has_tracks = false;

        uint32_t pos_ms = 0;
        uint32_t total_ms = 0;
        bool loops = false;
        bool seekable = false;
        uint32_t shown_sec = 0xFFFFFFFF;
        int shown_px = -1;

        bool seeking = false;
        uint32_t seek_ms = 0;
        int8_t pressed = -1;

        // キー/コントローラーで選んでいる所。0=シークバー 1〜5=ボタン
        int8_t focus_slot = 1 + kPlay;

        std::function<void(int)> on_button = nullptr;
        std::function<void(uint32_t)> on_seek = nullptr;
        std::function<void()> on_text_tap = nullptr;
        // pressed がこの値なら文字の所を押している
        static constexpr int8_t kTextArea = kButtons;

        static constexpr int kPad = 6;
        static constexpr int kTitleY = 5;
        static constexpr int kSubY = 23;
        static constexpr int kTimeY = 59;
        static constexpr int kTimeW = 44;       // 今の時間の欄(「12:34」)
        static constexpr int kTotalW = 54;      // 長さの欄(「ループ」が収まる幅)
        static constexpr int kButtonsY = 78;
        static constexpr int kButtonsH = 38;
};
