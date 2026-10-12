// TouchCalib(タッチ位置の補正のアフィン変換)のテスト。ハードウェアに依存しない計算だけを確かめる。
#include "util/TouchCalib.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    const int W = 240, H = 320;
    const float px[5] = {20, 220, 20, 220, 120};
    const float py[5] = {42, 42, 300, 300, 171};

    // 既定の式は昔のmap()と同じ: 生の値の範囲の端が画面の端に当たる
    {
        const TouchCalib::Affine d = TouchCalib::Default(W, H);
        float x, y;
        TouchCalib::Apply(d, TouchCalib::kMinU, TouchCalib::kMinV, x, y);
        check(std::fabs(x - (W - 1)) < 0.01f && std::fabs(y) < 0.01f, "既定: u=min,v=min は (W-1, 0)");
        TouchCalib::Apply(d, TouchCalib::kMaxU, TouchCalib::kMaxV, x, y);
        check(std::fabs(x) < 0.01f && std::fabs(y - (H - 1)) < 0.01f, "既定: u=max,v=max は (0, H-1)");

        float u, v;
        TouchCalib::DefaultInverse(W, H, 100, 200, u, v);
        TouchCalib::Apply(d, u, v, x, y);
        check(std::fabs(x - 100) < 0.01f && std::fabs(y - 200) < 0.01f, "既定の逆変換で往復できる");
    }

    // ずれ・拡大・傾きを持った「本当の」パネルを作り、その生の値から補正を求める
    {
        // 真の変換 X = a*u + b*v + c ... を適当に作る(既定から少しずれ、わずかに傾いた)
        TouchCalib::Affine truth = TouchCalib::Default(W, H);
        truth.a *= 1.06f; truth.b = 0.004f; truth.c -= 9.0f;
        truth.e *= 0.95f; truth.d = -0.003f; truth.f += 14.0f;

        // 真の変換の逆から、画面の点を押したときの生の値を作る
        const double det = (double)truth.a * truth.e - (double)truth.b * truth.d;
        float u[5], v[5];
        for(int i = 0; i < 5; i++){
            const double dx = px[i] - truth.c, dy = py[i] - truth.f;
            u[i] = (float)((truth.e * dx - truth.b * dy) / det);
            v[i] = (float)((-truth.d * dx + truth.a * dy) / det);
        }

        TouchCalib::Affine t;
        check(TouchCalib::Fit(u, v, px, py, 5, t), "5点から変換を求められる");
        float mx, me;
        TouchCalib::Residual(t, u, v, px, py, 5, mx, me);
        check(mx < 0.05f, "ノイズ無しなら誤差はほぼ0");

        // 補正前(既定)ではこのパネルが何px外れるか→補正後は小さい
        float dmx, dme;
        TouchCalib::Residual(TouchCalib::Default(W, H), u, v, px, py, 5, dmx, dme);
        check(dmx > 8.0f, "補正前は大きくずれていた");
        check(mx < dmx / 50.0f, "補正でずれが大幅に減る");

        // 点以外の位置でも合っている
        float x, y;
        const double qx = 77, qy = 233;
        const double ux = (truth.e * (qx - truth.c) - truth.b * (qy - truth.f)) / det;
        const double vx = (-truth.d * (qx - truth.c) + truth.a * (qy - truth.f)) / det;
        TouchCalib::Apply(t, (float)ux, (float)vx, x, y);
        check(std::fabs(x - qx) < 0.3f && std::fabs(y - qy) < 0.3f, "押した点以外の位置も合う");
    }

    // ノイズ(±2程度)が乗っても、最小二乗で平均的に合う
    {
        const TouchCalib::Affine d = TouchCalib::Default(W, H);
        float u[5], v[5];
        srand(1);
        for(int i = 0; i < 5; i++){
            TouchCalib::DefaultInverse(W, H, px[i], py[i], u[i], v[i]);
            u[i] += (float)(rand() % 41 - 20);   // 生の値のノイズ(±20 = 約±1.4px)
            v[i] += (float)(rand() % 41 - 20);
        }
        TouchCalib::Affine t;
        check(TouchCalib::Fit(u, v, px, py, 5, t), "ノイズ入りでも求められる");
        float mx, me;
        TouchCalib::Residual(t, u, v, px, py, 5, mx, me);
        check(mx < 2.0f, "ノイズ入りでも誤差は2px未満");
        (void)d;
    }

    // 決まらない場合: 同じ位置ばかり/一直線の点
    {
        float u[3] = {1000, 1000, 1000}, v[3] = {2000, 2000, 2000};
        float X[3] = {10, 20, 30}, Y[3] = {10, 20, 30};
        TouchCalib::Affine t;
        check(!TouchCalib::Fit(u, v, X, Y, 3, t), "同じ生の値の3点は断る");
        float u2[4] = {1000, 1500, 2000, 2500}, v2[4] = {1000, 1500, 2000, 2500};
        float X2[4] = {10, 20, 30, 40}, Y2[4] = {10, 20, 30, 40};
        check(!TouchCalib::Fit(u2, v2, X2, Y2, 4, t), "一直線の点は断る");
        check(!TouchCalib::Fit(u, v, X, Y, 2, t), "2点は断る");
    }

    // 押し間違い(1点だけ大きく外れる)は残差が大きく出て見分けられる
    {
        float u[5], v[5];
        for(int i = 0; i < 5; i++) TouchCalib::DefaultInverse(W, H, px[i], py[i], u[i], v[i]);
        u[2] += 400;   // 約17pxぶん別の所を押した
        TouchCalib::Affine t;
        check(TouchCalib::Fit(u, v, px, py, 5, t), "押し間違いでも一応求まる");
        float mx, me;
        TouchCalib::Residual(t, u, v, px, py, 5, mx, me);
        check(mx > 5.0f, "押し間違いは残差に出る");
    }

    printf(failures ? "\n%d 件失敗\n" : "\n全部成功\n", failures);
    return failures ? 1 : 0;
}
