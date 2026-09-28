#pragma once

#include <cstdint>

// タッチ座標の単発ノイズ(1フレームだけ実際の位置から外れた値が混じる「スパイク」)を
// 抑える median-of-5 フィルタ。ペイント系アプリで滑らかに描いたはずの線に、規則的な
// 棘(とげ)状の乱れが出る問題(2026-09-28、実機でバッテリー駆動時に発見)への対策。
//
// - **移動平均ではなく中央値**にしてある。移動平均だと外れ値も数値としてブレンドされて
//   しまい、線そのものが常に細かく波打つ(=スパイクの影響が薄まって広がるだけ)。
//   中央値なら「5点のうち明らかにおかしい1〜2点」をほぼ完全に無視できる。
// - **当初はmedian-of-3だったが、実機での検証(TFT/タッチのSPIクロックを下げても
//   ノイズが完治しない。ブレッドボード+ジャンパワイヤの接触不良が濃厚)を受けて
//   median-of-5へ広げた(2026-09-28)。窓を広げるほど単発ノイズへは強くなるが、
//   実際の動きへの追従の遅れも増える(3点なら約1フレーム、5点なら約2フレームの遅れ)。
//   いつか基板(ハンダ付け)へ移行してノイズ源そのものが減れば、再び窓を狭める余地がある**。
// - 5点(直近5フレーム)だけの固定サイズ。RAMも計算も一定(確保なし)。
// - x/yを独立に中央値化する(2次元の幾何学的中央値ではない)。実装が単純で、
//   XPT2046のノイズはX/Yそれぞれの軸で独立に起きるため、この簡略化で実用上十分。
// - プラットフォームに依存しない(実機のTouch_Functions.hppだけで使う。PCビルドの
//   マウス入力はノイズが無いため対象外)。ホストテストで単体検証できるよう、
//   ハードウェア依存を一切持たない
class TouchFilter {
    private:
        static constexpr int kWindow = 5;

        int16_t hx[kWindow] = {0, 0, 0, 0, 0};
        int16_t hy[kWindow] = {0, 0, 0, 0, 0};
        bool    primed = false;

        // kWindow個(=5、小さい固定数)の中央値。要素数が少ないので単純な挿入ソートで
        // 十分速い(呼び出しは1フレームにつきx/y各1回)。元の配列は書き換えない
        static int16_t Median(const int16_t (&src)[kWindow]){
            int16_t tmp[kWindow];
            for(int i = 0; i < kWindow; i++) tmp[i] = src[i];

            for(int i = 1; i < kWindow; i++){
                const int16_t key = tmp[i];
                int j = i - 1;
                while(j >= 0 && tmp[j] > key){
                    tmp[j + 1] = tmp[j];
                    j--;
                }
                tmp[j + 1] = key;
            }
            return tmp[kWindow / 2];
        }

    public:
        // 新しいタッチ(タッチ開始)のたびに呼ぶこと。履歴をこの1点で埋め直すので、
        // 前のタッチの名残りが新しいタッチの最初の数フレームへ混ざらない。
        // 呼んだ直後は座標(x,y)自体を出力としてそのまま使ってよい
        // (窓の全点が同じ値なので中央値も同じ値になる)
        void reset(int16_t x, int16_t y){
            for(int i = 0; i < kWindow; i++){
                hx[i] = x;
                hy[i] = y;
            }
            primed = true;
        }

        // 生の座標を1つ足し、直近kWindowフレームの中央値をout_x/out_yへ返す。
        // reset()を一度も呼んでいなければ、この座標で自動的にprimeする
        void push(int16_t x, int16_t y, int16_t& out_x, int16_t& out_y){
            if(!primed) reset(x, y);

            for(int i = kWindow - 1; i > 0; i--){
                hx[i] = hx[i - 1];
                hy[i] = hy[i - 1];
            }
            hx[0] = x;
            hy[0] = y;

            out_x = Median(hx);
            out_y = Median(hy);
        }
};
