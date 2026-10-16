#pragma once

#include <cstdint>

#include "util/Rect.hpp"

// ホーム画面(ランチャ)に置くウィジェット1種類ぶんの描き方と操作。
//
// ランチャ(AppGrid)はウィジェットごとに子ウィジェットを作らず、決まったマス目の矩形を渡して
// draw() で直接 OSData::frame へ描かせる(アプリのタイルと同じ「render()で直接描く」型)。
// 実体は種類ごとに1つだけの静的なオブジェクトで、同じ種類を2つ置いても状態は共有する
// (どれも OS の状態を読んで見せるだけなので、置いたものごとの状態は要らない)。
//
// - stamp() … 見た目が変わるたびに変わる値。AppGrid が毎フレーム見て、変わったウィジェットの矩形だけを描き直す
// - buttonAt()/press() … ウィジェットの中のボタン(音楽の 前/再生/次 等)。ボタンの外を押したら appName() のアプリを開く
// - 描くのは FlushDirty() の合成の中だけ(今のクリップ=dirty矩形の内側)。r は画面座標
class HomeWidget {
    public:
        virtual ~HomeWidget() = default;

        // 押すと開くアプリ(ランチャの登録簿の名前)。無ければ nullptr
        virtual const char* appName() const { return nullptr; }
        // 見た目が変わったら変わる値(w,h はマス目の数)
        virtual uint32_t stamp(uint8_t w, uint8_t h) = 0;
        // 描く。pressed_button は押しているボタン(無ければ-1)
        virtual void draw(const Rect& r, uint8_t w, uint8_t h, int pressed_button) = 0;
        // r の中の (x,y)(画面座標)にあるボタン。無ければ-1
        virtual int buttonAt(const Rect& r, uint8_t w, uint8_t h, int x, int y) { (void)r; (void)w; (void)h; (void)x; (void)y; return -1; }
        // ボタンを押して離した
        virtual void press(int button) { (void)button; }
        // ランチャを開いたとき(SDから読み直す等。重い仕事はここで)
        virtual void onHomeEnter() {}
        // ランチャを開いている間、毎フレーム(置いてある種類ごとに1回。日付が変わったら読み直す等)
        virtual void tick() {}
};
