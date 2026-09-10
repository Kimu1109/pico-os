#pragma once

#include "Arduino.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

class ITextInputTarget;

//テキストの入力を行うウィジェットクラス
//入力したテキストを簡便に取得するために必要
class ITextInputWidget {
    public:
        virtual FixedString<PICO_STR_LL> getText() = 0;
        virtual void setText(const FixedString<PICO_STR_LL>& text) = 0;

        virtual void setInputTarget(ITextInputTarget* target) = 0;
        virtual void removeInputTarget(ITextInputTarget* valid_target) = 0;
        virtual ITextInputTarget* getInputTarget() = 0;
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

        virtual bool getIsSingleLine() = 0;
        virtual void setIsSingleLine(bool is_single_line) = 0;

        virtual ~ITextInputTarget() = default;
};