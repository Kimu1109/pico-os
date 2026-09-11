#pragma once

#include "util/Rect.hpp"
#include "consts.hpp"

// 画面(シーン)1枚を表す基底クラス。
//
// レイヤとシーンの関係(WidgetFunctionsの3レイヤ構造に対応):
//   - widgets      (通常レイヤ)     … シーンの所有物。遷移時に全て破棄される
//   - dialog_roots (ダイアログ層)   … シーンの所有物。遷移時に全て破棄される
//                                     (シーン内ウィジェットのthisを捕捉しているため残せない)
//   - overlays     (オーバーレイ層) … OS常駐。ステータスバー/キーボードはシーンをまたいで生き続ける
//
// つまり「常駐させたいものはAddOverlay()に置く」というのがシーン遷移における唯一のルール。
//
// ウィジェットの寿命はシーンがアクティブな間だけ。スタックに退避されたシーンも
// ウィジェットは解放済みで、Pop()で戻ってきた時にonEnter()から作り直される
// (シーンオブジェクト自体は数十バイトなので積んだままでもRAMを食わない)。
class Scene {
    public:
        virtual ~Scene() = default;

        // ログ表示用のシーン名
        virtual const char* getName() const = 0;

        // 表示開始時。ここでウィジェットをnewし、WidgetFunctions::Add()で通常レイヤへ登録する。
        // Pop()で戻ってきた場合も再度呼ばれるので、状態はメンバに持たせて復元すること
        virtual void onEnter() = 0;

        // 表示終了時。ウィジェット本体の破棄はフレームワーク(SceneFunctions)が一括で行うので、
        // ここでは「自分が持っている生ポインタのnull化」と「次回復元したい状態の保存」だけを行う。
        // ここでdeleteしてはいけない(二重解放になる)
        virtual void onExit() {}

        // アクティブなシーンのみ毎フレーム呼ばれる。
        // ウィジェットの描画とは独立した軽い処理(ポーリング等)向け
        virtual void onUpdate() {}

        // ステータスバーを除いたシーンが自由に使える領域
        static constexpr Rect contentRect() {
            return {
                0,
                STATUSBAR_HEIGHT,
                SCREEN_WIDTH,
                (int16_t)(SCREEN_HEIGHT - STATUSBAR_HEIGHT)
            };
        }
};
