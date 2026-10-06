#pragma once

#include "util/Rect.hpp"
#include "LovyanGFX.h"

namespace PICO_GFX {

    inline const static int COLORS[16] = {
        TFT_BLACK,
        TFT_NAVY,
        TFT_DARKGREEN,
        TFT_DARKCYAN,
        TFT_MAROON,
        TFT_PURPLE,
        TFT_OLIVE,
        TFT_LIGHTGREY,
        TFT_DARKGREY,
        TFT_BLUE,
        TFT_GREEN,
        TFT_CYAN,
        TFT_RED,
        TFT_MAGENTA,
        TFT_YELLOW,
        TFT_WHITE
    };

    inline Rect directRenderRect = {0, 0, 0, 0};
    inline bool enableDirectRender = false;

    // dirty矩形は確保ゼロの固定長配列で持つ(FlushDirty()のたびにヒープを触らないため)。
    // シーン遷移直後は48件を軽く超える(実測)ので、余裕を見て128件に広げた。
    // それでも溢れる場合(kMaxDirtyRectsを超えてMarkDirty()された場合)は、個々の矩形を
    // 追うのを諦めて画面全体を1枚のdirty矩形として転送する(FlushDirty()参照)。
    constexpr int kMaxDirtyRects = 128;
    inline Rect dirtyRects[kMaxDirtyRects];
    inline int dirtyRectCount = 0;
    inline bool dirtyOverflowed = false;
    inline bool isDirtyDeactivates;

    // MarkDirtyBelow()で積まれた矩形か(dirtyRectsと同じ添字)。FlushDirty()が、この矩形では
    // 「TRANSLUCENTの下は描き直さない」近道を使わず、一番下から描き直す
    inline bool dirtyForceBelow[kMaxDirtyRects];
    // 溢れて画面全体に切り替わったときの取りこぼし対策(1枚でもMarkDirtyBelow()があったか)
    inline bool dirtyForceBelowAny = false;

    // FlushDirty()がウィジェットのrender()を呼んでいる間、そのウィジェットに掛けているクリップ
    // (ウィジェットの矩形 ∩ dirty矩形)。pico.set_draw_area()はこの外へ広げない
    inline Rect render_clip = {0, 0, 0, 0};
    inline bool render_clip_active = false;

    void Setup();
    void MarkDirty(const Rect& rect);

    // MarkDirty()と同じだが、この矩形の中は「半透明(TRANSLUCENT)のウィジェットの下」まで
    // 描き直させる。半透明のダイアログやキーボードの上に一時的に重ねていたもの(通知のトースト)を
    // 消すとき用。普通のMarkDirty()だと、半透明の下は変わっていない前提で描き直されないため、
    // 消したものの絵がダイアログの下に残る。
    // ヘッダに置いているのは、MarkDirty()を偽物に差し替えるホストテストでもそのまま使えるように
    inline void MarkDirtyBelow(const Rect& rect){
        if(isDirtyDeactivates || rect.w <= 0 || rect.h <= 0) return;
        const int before = dirtyRectCount;
        MarkDirty(rect);
        if(dirtyRectCount == before + 1) dirtyForceBelow[before] = true;
        dirtyForceBelowAny = true;
    }

    void FlushDirty();

    // FlushDirty()の中身(ホストテストやデバッグ用に外から見えるようにしてある)。
    // 重なる/近いdirty矩形を1枚にまとめる
    void CoalesceDirtyRects();
    // 液晶へ送る: 矩形の中の、前に送った内容から変わった行だけを送る(forceなら全部)。送った画素数を返す
    void BeginRowCompare();
    uint32_t PushChangedRows(const Rect& d, bool force);
    void EndRowCompare();
    // frameを通さずに液晶へ描いたとき、その行を「次は必ず送る」にする
    void InvalidateLcdRows(int y, int h);
#if defined(PICOOS_PC)
    // PCビルドの確認用(PICOOS_VERIFY_LCD=1): 液晶の中身がframeと一致するか全画素比べてログへ出す
    void VerifyLcdMatchesFrame();
#endif

    void DrawDialogBackground();

    // 画面の明るさ(0〜100)。バックライト(TFT_LED)のPWMデューティ比を変える(DisplayFunctions参照)。
    // 描画内容には触れないので再描画は要らない。
    void SetBrightness(uint8_t percent);
    uint8_t GetBrightness();
}