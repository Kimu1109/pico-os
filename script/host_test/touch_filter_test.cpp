// TouchFilter(タッチ座標のmedian-of-5ノイズ抑制)のテスト。
//
// 実機依存が一切無い純粋なロジックなので、ホストでそのまま検証できる。
// 実際の効果(ブレッドボード配線でのノイズがどこまで収まるか)は実機での確認が要る
// (このホストテストが保証するのは、フィルタのアルゴリズム自体が意図通り
// 動くことだけ)。
#include "util/TouchFilter.hpp"
#include <cstdio>
#include <cstdlib>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    // ---- 単発のスパイクをほぼ無視する(5点中1点なら中央値は動じない) ----
    {
        TouchFilter f;
        int16_t x, y;
        f.reset(100, 100);
        f.push(102, 101, x, y);
        f.push(104, 102, x, y);
        const int16_t before_spike_x = x;
        const int16_t before_spike_y = y;

        //ここで1フレームだけ大きく外れた値が来る(典型的なノイズ)
        f.push(9999, 1, x, y);
        check(x != 9999 && y != 1, "スパイクの生値がそのまま出力されない");
        check(abs((int)x - (int)before_spike_x) <= 20, "スパイク直後も出力がほぼ動かない(x)");
        check(abs((int)y - (int)before_spike_y) <= 20, "スパイク直後も出力がほぼ動かない(y)");
    }

    // ---- 2フレーム連続のノイズにもある程度耐える(5点中2点までは少数派) ----
    {
        TouchFilter f;
        int16_t x, y;
        f.reset(200, 200);
        f.push(9999, 1, x, y);
        f.push(9998, 2, x, y);
        //5点中2点が外れ値、3点が200のままなので中央値は依然200のまま
        check(x == 200 && y == 200, "2フレーム連続のスパイクでも多数派(3/5)が勝つ");
    }

    // ---- 滑らかな動きはほぼそのまま通る(過剰に遅れない) ----
    {
        TouchFilter f;
        int16_t x, y;
        f.reset(0, 0);
        int16_t last_x = 0, last_y = 0;
        bool monotonic = true;
        for(int i = 1; i <= 20; i++){
            f.push((int16_t)(i * 2), (int16_t)(i * 3), x, y);
            if(x < last_x || y < last_y) monotonic = false;
            last_x = x; last_y = y;
        }
        check(monotonic, "単調に動く入力は出力も単調(過剰な振動が無い)");
        //窓が広い(5点)ぶんmedian-of-3より遅れは増えるが、20フレームも動けば十分追従する
        check(last_x >= 30 && last_y >= 45, "滑らかな動きへの追従に致命的な遅れが無い");
    }

    // ---- reset()は新しいタッチのたびに履歴をその場でリセットする ----
    {
        TouchFilter f;
        int16_t x, y;
        f.reset(200, 50);
        f.push(201, 51, x, y);
        f.push(199, 49, x, y);

        //別のタッチが離れた位置で始まる(前のタッチの履歴が残っていると
        //最初の数フレームが引っ張られてしまう)
        f.reset(10, 10);
        f.push(11, 11, x, y);
        //reset(10,10)で5点とも10/10に揃っているので、中央値は10,10,10,10,11→10
        check(x == 10 && y == 10, "reset()直後は前のタッチの名残りが混ざらない");
    }

    // ---- push()をreset()無しでいきなり呼んでも安全(自動prime) ----
    {
        TouchFilter f;
        int16_t x, y;
        f.push(77, 88, x, y);
        check(x == 77 && y == 88, "reset()を呼ばずにpush()しても最初の値がそのまま出る");
    }

    printf("\n");
    if(failures == 0){
        printf("ALL PASSED (failures=0)\n");
        return 0;
    }else{
        printf("FAILED: failures=%d\n", failures);
        return 1;
    }
}
