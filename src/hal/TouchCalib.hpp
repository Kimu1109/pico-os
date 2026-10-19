#pragma once

#include <cmath>
#include <cstdint>

// タッチ位置の補正(キャリブレーション)。XPT2046の生の値(u, v)を画面座標(X, Y)へ変える
// アフィン変換を持ち、画面上の複数の点を実際にタップしてもらって最小二乗法で求める。
//
//   X = a*u + b*v + c
//   Y = d*u + e*v + f
//
// (u, v) は XPT2046_Touchscreen の (p.y, p.x)。つまり実機の Touch_Functions が昔から
// 「p.y→横、p.x→縦」と入れ替えて map() していた向きに揃えてある。
// 既定値(Default())はその map() と同じ式なので、補正をしていなくても今までと同じ座標になる。
//
// ハードウェアにも描画にも依存しない(ホストテストで単体検証できる)。
// ファイルへの保存は hal/TouchCalib_Functions.hpp。
namespace TouchCalib {
    // 昔の map(p.y, MINY, MAXY, W-1, 0) / map(p.x, MINX, MAXX, 0, H-1) の定数
    constexpr int kMinU = 300;   // p.y の下限
    constexpr int kMaxU = 3800;  // p.y の上限
    constexpr int kMinV = 300;   // p.x の下限
    constexpr int kMaxV = 3800;  // p.x の上限

    struct Affine {
        float a, b, c, d, e, f;
    };

    // 補正を求めるときの点の数の上限
    constexpr int kMaxPoints = 8;

    // 補正をしていないときの変換(旧来の map() と同じ)
    inline Affine Default(int screen_w, int screen_h){
        const float w = (float)(screen_w - 1);
        const float h = (float)(screen_h - 1);
        Affine r;
        r.a = -w / (float)(kMaxU - kMinU);
        r.b = 0.0f;
        r.c = w + (float)kMinU * w / (float)(kMaxU - kMinU);
        r.d = 0.0f;
        r.e = h / (float)(kMaxV - kMinV);
        r.f = -(float)kMinV * h / (float)(kMaxV - kMinV);
        return r;
    }

    inline void Apply(const Affine& t, float u, float v, float& x, float& y){
        x = t.a * u + t.b * v + t.c;
        y = t.d * u + t.e * v + t.f;
    }

    // Default() の逆変換: 画面座標から「その位置を押したときの生の値」を作る。
    // PCビルド(マウス)が実機と同じ形の生の値を出すために使う
    inline void DefaultInverse(int screen_w, int screen_h, float x, float y, float& u, float& v){
        const Affine d = Default(screen_w, screen_h);
        u = (x - d.c) / d.a;
        v = (y - d.f) / d.e;
    }

    // 3x3 の連立方程式 M*s = r をクラメルの公式で解く。行列が特異ならfalse
    inline bool Solve3(const double m[3][3], const double r[3], double s[3]){
        auto det = [](const double k[3][3]){
            return k[0][0] * (k[1][1] * k[2][2] - k[1][2] * k[2][1])
                 - k[0][1] * (k[1][0] * k[2][2] - k[1][2] * k[2][0])
                 + k[0][2] * (k[1][0] * k[2][1] - k[1][1] * k[2][0]);
        };
        const double d = det(m);
        if(std::fabs(d) < 1e-9) return false;
        for(int col = 0; col < 3; col++){
            double k[3][3];
            for(int i = 0; i < 3; i++)
                for(int j = 0; j < 3; j++)
                    k[i][j] = (j == col) ? r[i] : m[i][j];
            s[col] = det(k) / d;
        }
        return true;
    }

    // 生の値 (u[i], v[i]) を押したとき画面の (X[i], Y[i]) だった、という n 点(3以上)から
    // 最小二乗法で変換を求める。点が一直線に並ぶ等で決まらなければfalse。
    // 値の大きさ(u,v は千単位)で桁落ちしないよう、平均を引いてから解く
    inline bool Fit(const float* u, const float* v, const float* X, const float* Y, int n, Affine& out){
        if(n < 3 || n > kMaxPoints) return false;

        double mu = 0, mv = 0;
        for(int i = 0; i < n; i++){ mu += u[i]; mv += v[i]; }
        mu /= n; mv /= n;

        double m[3][3] = {};
        double rx[3] = {}, ry[3] = {};
        for(int i = 0; i < n; i++){
            const double p[3] = { u[i] - mu, v[i] - mv, 1.0 };
            for(int j = 0; j < 3; j++){
                for(int k = 0; k < 3; k++) m[j][k] += p[j] * p[k];
                rx[j] += p[j] * X[i];
                ry[j] += p[j] * Y[i];
            }
        }

        double sx[3], sy[3];
        if(!Solve3(m, rx, sx) || !Solve3(m, ry, sy)) return false;

        // 平均を引いた分を定数項へ戻す
        out.a = (float)sx[0]; out.b = (float)sx[1];
        out.c = (float)(sx[2] - sx[0] * mu - sx[1] * mv);
        out.d = (float)sy[0]; out.e = (float)sy[1];
        out.f = (float)(sy[2] - sy[0] * mu - sy[1] * mv);
        return true;
    }

    // 求めた変換が各点で何px外れるか(距離)。最大値を max_err、平均を mean_err へ
    inline void Residual(const Affine& t, const float* u, const float* v, const float* X, const float* Y,
                         int n, float& max_err, float& mean_err){
        max_err = 0; mean_err = 0;
        for(int i = 0; i < n; i++){
            float x, y;
            Apply(t, u[i], v[i], x, y);
            const float e = std::sqrt((x - X[i]) * (x - X[i]) + (y - Y[i]) * (y - Y[i]));
            if(e > max_err) max_err = e;
            mean_err += e;
        }
        if(n > 0) mean_err /= (float)n;
    }

    // 今使っている変換。Touch_Functions が毎フレーム使い、TouchCalibFunctions が読み書きする。
    // 起動直後の値は setup() の中の Reset() で入る(ここでは画面の大きさを知らないので0のまま)
    inline Affine current = {0, 0, 0, 0, 0, 0};
    inline bool has_current = false;
}
