#pragma once

#include "gui/widgets/Widget.hpp"

#include <vector>

namespace WidgetFunctions
{
    inline std::vector<Widget*> widgets;
    inline std::vector<Widget*> dialog_roots;
    inline std::vector<Widget*> overlays;
    inline std::vector<Widget*> pending_deletes;

    inline Widget *pressingWidget = nullptr;

    void Setup();

    void Add(Widget *w);
    void AddDialog(Widget *w);
    void AddOverlay(Widget *w);

    void Remove(Widget *w);
    void RemoveDialog(Widget *w);
    void RemoveOverlay(Widget *w);
    void RemoveAny(Widget *w);

    void Destroy(Widget *w);
    void DestroyLater(Widget *w);

    void ProcessPendingDeletes();

    // シーンが所有するウィジェット(通常レイヤ + ダイアログ層)を全て破棄する。
    // オーバーレイ層(ステータスバー/キーボード)は常駐なので触らない。
    // フレーム途中(ウィジェットのコールバック内)から呼ぶと自分自身を破棄しかねないため、
    // 呼び出しはSceneFunctions::Update()のフレーム境界からのみとする
    void ClearSceneWidgets();

    void BringToFront(Widget *w);

    void UpdateAll();

    // タッチは上から順に判定
    Widget *HitTest(int16_t x, int16_t y);
};