#include "functions/Focus_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "OS_Data.hpp"

// 使い方と決まりは Focus_Functions.hpp。
// 候補は毎回リストを走査して選ぶ(並べた配列を持たない)。1回の操作で数十個を数回なめるだけ

namespace {
    // 一番上に開いているダイアログ(無ければnullptr=画面が範囲)
    Widget* TopDialog(){
        for(int d = (int)WidgetFunctions::dialog_roots.size() - 1; d >= 0; d--){
            Widget* w = WidgetFunctions::dialog_roots[d];
            if(w && w->getVisible()) return w;
        }
        return nullptr;
    }

    // 範囲の中の候補を順に(リストの順。ダイアログは親→子の順)
    template<typename F>
    void ForEachCandidate(F&& f){
        Widget* top = TopDialog();
        if(top){
            top->visitAll([&f](Widget* w){
                if(FocusFunctions::IsCandidate(w)) f(w);
            });
            return;
        }
        // コールバックの中でリストが変わることは無い(読むだけ)
        for(Widget* w : WidgetFunctions::widgets){
            if(FocusFunctions::IsCandidate(w)) f(w);
        }
    }

    // 読む順(上から、同じ高さなら左から)の比較
    bool ReadsBefore(const Rect& a, const Rect& b){
        if(a.y != b.y) return a.y < b.y;
        return a.x < b.x;
    }

    Widget* FirstInReadingOrder(bool last){
        Widget* best = nullptr;
        Rect best_r{0, 0, 0, 0};
        ForEachCandidate([&](Widget* w){
            const Rect r = w->getScreenRect();
            if(!best || (last ? ReadsBefore(best_r, r) : ReadsBefore(r, best_r))){
                best = w;
                best_r = r;
            }
        });
        return best;
    }

    // Tab / L R: 読む順で次/前。端まで行ったら反対の端へ回る
    Widget* Ordered(Widget* cur, bool next){
        const Rect c = cur->getScreenRect();
        Widget* best = nullptr;
        Rect best_r{0, 0, 0, 0};
        // 同じ位置に重なったものは、リストの順で cur の後/前のものを次とみなす
        bool passed_cur = false;
        Widget* same_pos = nullptr;
        ForEachCandidate([&](Widget* w){
            if(w == cur){ passed_cur = true; return; }
            const Rect r = w->getScreenRect();
            if(r.x == c.x && r.y == c.y){
                if(next ? (passed_cur && !same_pos) : !passed_cur) same_pos = w;
                return;
            }
            const bool beyond = next ? ReadsBefore(c, r) : ReadsBefore(r, c);
            if(!beyond) return;
            if(!best || (next ? ReadsBefore(r, best_r) : ReadsBefore(best_r, r))){
                best = w;
                best_r = r;
            }
        });
        if(same_pos) return same_pos;
        if(best) return best;
        Widget* wrap = FirstInReadingOrder(!next);
        return wrap == cur ? nullptr : wrap;
    }

    // 矢印 / 十字キー: その向きにある中で一番近いもの。
    // 中心がその向きにあるものだけを見て、「向きの距離 + 横ずれ×3」が一番小さいものを選ぶ
    // (横ずれは、向きと直角の方向で範囲が重なっていれば0)
    Widget* Spatial(Widget* cur, FocusFunctions::Move m){
        const Rect a = cur->focusRect();
        const int acx = a.x + a.w / 2, acy = a.y + a.h / 2;
        const bool horizontal = (m == FocusFunctions::Move::Left || m == FocusFunctions::Move::Right);
        const int sign = (m == FocusFunctions::Move::Right || m == FocusFunctions::Move::Down) ? 1 : -1;

        Widget* best = nullptr;
        int32_t best_score = 0;
        ForEachCandidate([&](Widget* w){
            if(w == cur) return;
            const Rect b = w->getScreenRect();
            const int dx = (b.x + b.w / 2) - acx;
            const int dy = (b.y + b.h / 2) - acy;
            const int primary = (horizontal ? dx : dy) * sign;
            if(primary <= 0) return;
            int perp;
            if(horizontal){
                const bool overlap = b.y < a.y + a.h && a.y < b.y + b.h;
                perp = overlap ? 0 : (dy < 0 ? -dy : dy);
            }else{
                const bool overlap = b.x < a.x + a.w && a.x < b.x + b.w;
                perp = overlap ? 0 : (dx < 0 ? -dx : dx);
            }
            const int32_t score = (int32_t)primary + (int32_t)perp * 3;
            if(!best || score < best_score){
                best = w;
                best_score = score;
            }
        });
        return best;
    }

    // ダイアログが開いて、今のフォーカスが範囲の外になった: 閉じたときに戻せるよう覚えて外す
    void LeaveScopeIfNeeded(){
        Widget* f = FocusFunctions::focused;
        if(!f || FocusFunctions::InScope(f)) return;
        if(FocusFunctions::saved_id == 0 && TopDialog()){
            FocusFunctions::saved_id = f->getId();
            FocusFunctions::saved_ring = FocusFunctions::ring_visible;
        }
        FocusFunctions::Set(nullptr, false);
    }

    // フォーカスが無い/枠が出ていない: 今のもの(無ければ最初のもの)に枠を出すだけ
    bool Reveal(bool last){
        Widget* w = FocusFunctions::Current();
        if(!w) w = FirstInReadingOrder(last);
        if(!w) return false;
        FocusFunctions::Set(w, true);
        return true;
    }

    // 「真ん中をタップした」として押す。押した中で自分が消えたら(~Widget()がfocusedを下ろす)離すのは配らない
    void TapCenter(Widget* w){
        Rect r = w->clippedScreenRect();
        if(r.w <= 0 || r.h <= 0) r = w->getScreenRect();
        const int save_x = OSData::touchX, save_y = OSData::touchY;
        OSData::touchX = r.x + r.w / 2;
        OSData::touchY = r.y + r.h / 2;
        w->causeOnPressStart();
        if(FocusFunctions::focused == w) w->causeOnPressEnd();
        OSData::touchX = save_x;
        OSData::touchY = save_y;
    }
}

bool FocusFunctions::IsCandidate(Widget* w){
    if(!w || !w->isFocusable() || w->getHitTransparent()) return false;
    if(!w->isEffectivelyEnabled()) return false;
    for(Widget* p = w; p; p = p->getParent()){
        if(!p->getVisible()) return false;
    }
    const Rect r = w->getScreenRect();
    return r.w > 0 && r.h > 0;
}

Widget* FocusFunctions::Current(){
    if(focused && InScope(focused) && IsCandidate(focused)) return focused;
    // ダイアログを閉じた後: 開く前のフォーカスへ戻す
    if(saved_id != 0 && !TopDialog()){
        Widget* w = WidgetRegistry::Resolve(saved_id);
        saved_id = 0;
        if(w && InScope(w) && IsCandidate(w)){
            const bool ring = ring_visible || saved_ring;
            saved_ring = false;
            Set(w, ring);
            return w;
        }
    }
    return nullptr;
}

void FocusFunctions::Set(Widget* w, bool show_ring){
    Widget* prev = focused;
    show_ring = show_ring && w;
    if(prev == w){
        if(show_ring != ring_visible){
            ring_visible = show_ring;
            MarkRing(w);
        }
    }else{
        if(ring_visible) MarkRing(prev);
        focused = w;
        ring_visible = show_ring;
        if(prev) prev->onFocusChanged(false);
        if(w) w->onFocusChanged(true);
    }
    if(!w) return;
    // 入れ物の中で隠れていれば見える所までスクロールする(内側の入れ物から順に)
    for(Widget* p = w->getParent(); p; p = p->getParent()){
        p->revealRect(w->getScreenRect());
    }
    if(ring_visible) MarkRing(w);
}

bool FocusFunctions::Navigate(Move m){
    LeaveScopeIfNeeded();
    Widget* cur = Current();
    if(!cur || !ring_visible) return Reveal(m == Move::Prev);
    Widget* next = (m == Move::Next || m == Move::Prev) ? Ordered(cur, m == Move::Next) : Spatial(cur, m);
    if(!next) return false;
    Set(next, true);
    return true;
}

bool FocusFunctions::Activate(){
    LeaveScopeIfNeeded();
    Widget* w = Current();
    if(!w || !ring_visible) return Reveal(false);

    Widget* dialog_before = TopDialog();
    if(!w->onFocusKey(FocusKey::Activate)) TapCenter(w);

    // 押してダイアログが開いた/閉じた: 開いたら中の最初のものへ、閉じたら開く前のものへ
    Widget* dialog_after = TopDialog();
    if(dialog_after != dialog_before){
        LeaveScopeIfNeeded();
        if(!Current()){
            Widget* first = FirstInReadingOrder(false);
            if(first) Set(first, true);
        }else{
            Set(focused, true);
        }
    }
    return true;
}

bool FocusFunctions::SendKey(FocusKey key){
    if(key == FocusKey::Activate) return Activate();
    LeaveScopeIfNeeded();
    Widget* w = Current();
    if(!w || !ring_visible){
        if(key == FocusKey::Back) return false;
        return Reveal(false);
    }
    if(w->onFocusKey(key)) return true;
    switch(key){
        case FocusKey::Up:    return Navigate(Move::Up);
        case FocusKey::Down:  return Navigate(Move::Down);
        case FocusKey::Left:  return Navigate(Move::Left);
        case FocusKey::Right: return Navigate(Move::Right);
        default:              return false;
    }
}

void FocusFunctions::Update(){
    if(ring_visible && focused && !InScope(focused)){
        //枠が出ている間にダイアログが開いた
        LeaveScopeIfNeeded();
        Reveal(false);
    }else if(saved_id != 0 && !TopDialog()){
        //ダイアログが閉じた
        Current();
    }
}
