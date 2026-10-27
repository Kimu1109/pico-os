#pragma once

#include "gui/widgets/Widget.hpp"
#include "consts.hpp"

#include "util/InplaceFunction.hpp"

// 再生の操作ボタン3つ: 10秒戻る / 再生・一時停止 / 10秒進む。ミュージックアプリ(MusicPlayerPanel)と同じ見た目で、
// 絵(三角・棒・黒い丸)は自分で描く(tablerに再生系のアイコンが無いため)。ファイルビューワーと動画アプリが使う。
// 子を持たずrender()で直接描き、タップ位置から逆算する型。再生の状態は持たず、シーンが set*() で流し込む。
class MediaButtons : public Widget {
    public:
        enum Button : uint8_t { kRew, kPlay, kFwd, kButtons };
        static constexpr int kHeight = 38;

        MediaButtons(int x, int y, int w){
            this->l_rect = {(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)kHeight};
        }

        // showing_pause=今「一時停止」の絵を出す(再生中)
        void setShowPause(bool on);
        // 戻す/進めるを押せるか(MMLや何も開いていないときは偽)
        void setSkipEnabled(bool on);
        void setPlayEnabled(bool on);
        void setOnButton(InplaceFunction<void(int)> cb){ on_button = cb; }

        WidgetType getWidgetType() const override { return WidgetType::MediaButtons; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        void render() override;
        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void causeOnPressOut() override;

        bool focusableByDefault() const override { return true; }
        bool drawsOwnFocus() const override { return true; }
        bool onFocusKey(FocusKey key) override;
        Rect focusRect() const override;

        Rect buttonRect(int index) const;   // 部品内の座標

    private:
        int buttonAt(int lx, int ly) const;
        bool enabled(int index) const;
        void drawGlyph(int index, int cx, int cy, int8_t color);

        bool show_pause = false;
        bool skip_enabled = false;
        bool play_enabled = true;
        int8_t pressed = -1;
        int8_t focus_slot = kPlay;
        InplaceFunction<void(int)> on_button = nullptr;
};
