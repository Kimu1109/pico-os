// 再生の順番(src/sound/Play_Queue.hpp)のテスト。ミュージックアプリの順番通り/ミックスとリピート。
//
// 確かめること: 順番通りの次/前・リピートしない/全曲/1曲・利用者の「次へ」は1曲リピートでも進む・
// ミックスは全部の曲を1回ずつ・今の曲を先頭に置く・切り替えても今の曲が変わらない・
// 一巡して混ぜ直したとき直前の曲がすぐ続かない・空のプレイリスト
#include "sound/Play_Queue.hpp"
#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// 並びが 0〜n-1 を1回ずつ含むか
static bool isPermutation(const PlayQueue& q, int n){
    bool seen[PlayQueue::kMax] = {};
    for(int i = 0; i < n; i++){
        const int t = q.orderAt(i);
        if(t < 0 || t >= n || seen[t]) return false;
        seen[t] = true;
    }
    return true;
}

int main(){
    {
        PlayQueue q;
        q.reset(0);
        check(q.current() == -1 && q.next(false) == -1 && q.prev() == -1, "空のプレイリスト");
        q.start(0);
        check(q.current() == -1, "空なら start しても何も無い");
    }
    {
        PlayQueue q;
        q.reset(3);
        q.start(1);
        check(q.current() == 1 && q.position() == 1, "順番通り: 途中から始める");
        check(q.next(false) == 2, "順番通り: 次");
        check(q.next(false) == -1 && q.current() == 2, "リピートしない: 最後の次は終わり(位置は動かない)");
        check(q.next(true) == -1, "リピートしない: 最後で「次へ」も終わり");
        check(q.prev() == 1 && q.prev() == 0 && q.prev() == 0, "前へ: 先頭では先頭のまま");
    }
    {
        PlayQueue q;
        q.reset(3);
        q.setRepeat(PlayQueue::Repeat::All);
        q.start(2);
        check(q.next(false) == 0, "全曲リピート: 最後の次は先頭");
        check(q.prev() == 2, "全曲リピート: 先頭の前は最後");
    }
    {
        PlayQueue q;
        q.reset(3);
        q.setRepeat(PlayQueue::Repeat::One);
        q.start(1);
        check(q.next(false) == 1 && q.next(false) == 1, "1曲リピート: 終わったら同じ曲");
        check(q.next(true) == 2, "1曲リピート: 「次へ」は次の曲");
    }
    {
        PlayQueue q;
        q.cycleRepeat();
        check(q.repeat() == PlayQueue::Repeat::All, "リピートの切り替え: しない→全曲");
        q.cycleRepeat();
        check(q.repeat() == PlayQueue::Repeat::One, "リピートの切り替え: 全曲→1曲");
        q.cycleRepeat();
        check(q.repeat() == PlayQueue::Repeat::Off, "リピートの切り替え: 1曲→しない");
    }
    {
        PlayQueue q;
        q.seed(12345);
        q.setShuffle(true);
        q.reset(10);
        check(isPermutation(q, 10), "ミックス: 全部の曲を1回ずつ");
        bool identity = true;
        for(int i = 0; i < 10; i++) if(q.orderAt(i) != i) identity = false;
        check(!identity, "ミックス: 並びが混ざる");

        q.start(7);
        check(q.current() == 7 && q.position() == 0 && isPermutation(q, 10), "ミックス: 選んだ曲を先頭に置く");
        // 最後まで: 10曲を1回ずつ鳴らす
        bool seen[10] = {};
        seen[7] = true;
        int played = 1;
        for(int t; (t = q.next(false)) >= 0;){ if(!seen[t]){ seen[t] = true; played++; } else { played = -100; } }
        check(played == 10, "ミックス: 一巡で全部の曲を1回ずつ鳴らして終わる");
    }
    {
        PlayQueue q;
        q.seed(99);
        q.reset(5);
        q.start(3);
        q.next(false);                       // 4
        q.setShuffle(true);
        check(q.current() == 4 && q.position() == 0 && isPermutation(q, 5), "ミックスへ切り替えても今の曲は変わらない");
        q.next(false);
        const int cur = q.current();
        q.setShuffle(false);
        check(q.current() == cur && q.position() == cur, "順番通りへ戻すと今の曲の位置から続く");
    }
    {
        // 一巡して混ぜ直したとき、直前の曲がすぐ続かない(何度か試す)
        bool ok = true;
        for(uint32_t s = 1; s < 200; s++){
            PlayQueue q;
            q.seed(s);
            q.setShuffle(true);
            q.setRepeat(PlayQueue::Repeat::All);
            q.reset(4);
            for(int i = 0; i < 3; i++) q.next(false);
            const int last = q.current();
            const int first = q.next(false);
            if(first == last || !isPermutation(q, 4) || q.position() != 0) ok = false;
        }
        check(ok, "全曲リピート+ミックス: 混ぜ直しても直前の曲は先頭に来ない");
    }
    {
        PlayQueue q;
        q.reset(1000);
        check(q.count() == PlayQueue::kMax, "上限を超える曲の数は上限で切る");
    }

    printf(failures ? "\n%d 件失敗\n" : "\n全部通りました\n", failures);
    return failures ? 1 : 0;
}
