#pragma once

#include "gui/widgets/interfaces/ITextInputTarget.hpp"

namespace KeyboardFunctions {
    void Setup();
    void RegisterInputTarget(ITextInputTarget *target);
    void UnregisterInputTarget(ITextInputTarget *target);

    // 表示中のキーボードを全て閉じる。
    // キーボードはオーバーレイ常駐なのでシーン遷移では破棄されないが、
    // 入力対象(シーン内のTextbox等)が破棄される前にonHideを届けておく必要がある
    void HideAll();
}