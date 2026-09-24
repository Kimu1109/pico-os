#pragma once

#include "gui/widgets/Widget.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

class GbEmu;

// Game Boyの画面(160x144)を1.5倍の240x216で描くウィジェット。GameBoyScene専用。
//
// 240x320の縦画面なら、1.5倍でちょうど横幅いっぱいになり、下に操作パッドの場所が残る。
// 拡大は「横2画素→3画素、縦2行→3行」の最近傍(左/上の画素を2回使う)。
//
// ---- 描き方 ----
// 1画面ぶん(51840画素)をwritePixel()で1つずつ描くと実機で数十msかかるので、
// OSData::frame(4bppのスプライト)のバッファへ直接書き込む。
// 元の1バイト(2bit×4画素)→描く6画素(3バイト)の対応表を先に作っておき、
// 1行はその表を40回引いて写すだけにしてある。
//
// 描くのはFlushDirty()の合成の中(PICO_GFX::isDirtyDeactivates中)だけ。
// UpdateAll()から来るrender()では何も描かずに戻る(描いても直後に合成でもう一度描くため)。
// 合成の中でも、今のクリップ(=dirty矩形)の内側しか書かない。バッファを直接いじるので
// setClipRect()が効かず、はみ出すと上に重なったダイアログの下を塗りつぶしてしまうため。
//
// エミュが描き終えたら onFrame() を呼ぶ。変わった行の範囲だけをdirtyにするので、
// 画面が止まっている間は液晶へ何も送らない。
class GameBoyView : public Widget {
    public:
        constexpr static int kViewW = 240;
        constexpr static int kViewH = 216;

    private:
        GbEmu* emu = nullptr;

        // 元の1バイト(4画素)→拡大後の6画素(4bitずつ、3バイト)
        uint8_t lut[256][3];

        // ROMを読み込んでいない間に真ん中へ出す文
        FixedString<PICO_STR_L> message;

        void buildLut();
        // 画面上のdy行目(0〜215)を line(120バイト)へ作る
        void buildLine(int dy, uint8_t* line) const;
        void drawMessage(const Rect& g);

    public:
        GameBoyView(int x, int y, GbEmu* emu);

        // エミュが1フレーム進めたあとに呼ぶ。変わった行だけdirtyにする
        void onFrame();

        // ROMを読み込み直したとき等、全体を描き直す
        void invalidate(){ this->needsRender(); }

        void setMessage(const char* text);

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::GameBoyView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
};
