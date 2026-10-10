#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetID.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"

// ウィジェットのフォーカス。物理キーボード・外部コントローラーで画面のボタン等を選んで押すためのもの。
//
// 持つ状態はこのファイルの3つだけ(約10バイト)。ウィジェット側は既存の詰め物の1バイト(focus_mode)と
// 仮想関数(onFocusKey()等。中身はフラッシュ)だけで、ウィジェット1つあたりのRAMは増えない。
//
// - 範囲: 一番上に開いているダイアログの中。ダイアログが無ければ画面(通常レイヤ)。
//   オーバーレイ(ステータスバー・キーボード)には移らない
// - 移り先: 受けるもの(Widget::isFocusable())で、見えていて有効なもの。
//   Tab / L R は読む順(上から、同じ高さなら左から)、矢印 / 十字キーはその向きで一番近いもの
// - 枠: キーかボタンで動かしたときだけ見せ、タッチで消す(タッチしたものがフォーカスを持つが枠は出さない)。
//   描くのはFlushDirty()の合成の最後(ウィジェットの上、範囲の外のものには描かない)
// - 配り先は KeyInput_Dispatch.cpp(打鍵は画面のonKey()・キー盤が取らなかったもの、
//   コントローラーは画面がScene::usesPad()でないときだけ)
//
// フォーカスを持つウィジェットが消えたら~Widget()がfocusedを下ろすので、ダングリングにはならない。
// ダイアログを開く前のフォーカスはWidgetIdで覚えておき、閉じた後に戻す(生ポインタだと破棄を見逃す)
namespace FocusFunctions {

    inline Widget* focused = nullptr;
    inline bool ring_visible = false;
    // ダイアログを開いたときに範囲の外になったフォーカス(閉じたら戻す)
    inline WidgetId saved_id = 0;
    // そのとき枠が出ていたか(戻したときにも出す)
    inline bool saved_ring = false;

    enum class Move : uint8_t { Next, Prev, Up, Down, Left, Right };

    // 枠の太さと色
    constexpr int kRingWidth = 2;
    constexpr int8_t kRingColor = PICO_BLUE;

    // wが一番上の範囲(開いているダイアログ、無ければ画面)にいるか
    inline bool InScope(Widget* w){
        if(!w) return false;
        Widget* root = w;
        while(root->getParent()) root = root->getParent();
        for(int d = (int)WidgetFunctions::dialog_roots.size() - 1; d >= 0; d--){
            Widget* top = WidgetFunctions::dialog_roots[d];
            if(top && top->getVisible()) return root == top;
        }
        // ダイアログが開いていない: 画面(通常レイヤ)のものだけ。閉じたダイアログ・オーバーレイは外
        for(Widget* d : WidgetFunctions::dialog_roots) if(d == root) return false;
        for(Widget* o : WidgetFunctions::overlays) if(o == root) return false;
        return true;
    }

    // 枠を描く相手(無ければnullptr)。FlushDirty()が見る
    inline Widget* RingTarget(){
        if(!ring_visible || !focused || focused->drawsOwnFocus()) return nullptr;
        if(!focused->getVisible() || !InScope(focused)) return nullptr;
        return focused;
    }

    // wの枠の所を描き直させる。半透明のダイアログの下になった枠も消えるよう、下から描き直す
    inline void MarkRing(Widget* w){
        if(!w) return;
        const Rect r = w->clippedScreenRect();
        if(r.w > 0 && r.h > 0) PICO_GFX::MarkDirtyBelow(r);
        if(w->drawsOwnFocus()) w->needsRender();
    }

    // ウィジェットが消える/外れる(~Widget()・WidgetFunctions::Remove系から)
    inline void OnWidgetGone(Widget* w){
        if(focused == w){
            focused = nullptr;
            ring_visible = false;
        }
    }

    // タッチで押したもの(WidgetFunctions::UpdateAll()から)。枠を消し、受けるものならフォーカスを移す
    inline void OnTouchStart(Widget* pressed){
        saved_ring = false;
        Widget* next = (pressed && pressed->isFocusable()) ? pressed : focused;
        if(next == focused && !ring_visible) return;
        Widget* prev = focused;
        if(ring_visible){
            ring_visible = false;
            MarkRing(prev);
        }
        if(next != prev){
            focused = next;
            if(prev) prev->onFocusChanged(false);
            if(next) next->onFocusChanged(true);
        }
    }

    // wが今フォーカスを受けられるか(見えている・有効・大きさがある)
    bool IsCandidate(Widget* w);
    // 今のフォーカス(範囲の外・見えない・無効ならnullptr)
    Widget* Current();
    // フォーカスを移す(nullptrで外す)。show_ring=trueなら枠を見せる。見える所までスクロールする
    void Set(Widget* w, bool show_ring);
    // 移す。フォーカスが無い/枠が出ていなければ、最初のものに枠を出すだけ。移れたらtrue
    bool Navigate(Move m);
    // フォーカスのあるものを押す(扱えばtrue)
    bool Activate();
    // キー(Up〜Back)をフォーカスのあるものへ。扱わない矢印は隣へ移る
    bool SendKey(FocusKey key);
    // 毎フレーム(KeyInputFunctions::Update()の後)。枠が出ている間にダイアログが開いた/閉じたら、
    // 枠を中の最初のもの/開く前のものへ移す(ダイアログが1フレーム遅れて開く画面のため)
    void Update();
}
