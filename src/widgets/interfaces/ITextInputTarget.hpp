#pragma once

#include "Arduino.h"

class ITextInputTarget;

//テキストの入力を行うウィジェットクラス
//入力したテキストを簡便に取得するために必要
class ITextInputWidget {
    public:
        virtual String GetText() = 0;
        virtual void SetText(String text) = 0;

        virtual void SetInputTarget(ITextInputTarget* target) = 0;
        virtual void RemoveInputTarget(ITextInputTarget* valid_target) = 0;
        virtual ITextInputTarget* GetInputTarget() = 0;
};

//テキストの入力を受けるターゲットクラス
//継承して使うべし
//キーボードが入力のイベントを伝搬するために必要
//KeyboardFunctionsのRegister/Unregister関数を使うべし
class ITextInputTarget {
    public:
        virtual void onShow(ITextInputWidget* keyboard) = 0;
        virtual void onTextChanged(ITextInputWidget* keyboard) = 0;
        virtual void onHide(ITextInputWidget* keyboard) = 0;

        virtual bool GetIsSingleLine() = 0;
        virtual void SetIsSingleLine(bool is_single_line) = 0;

        virtual ~ITextInputTarget() = default;
};