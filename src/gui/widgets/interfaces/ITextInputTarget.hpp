#pragma once

#include "Arduino.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

class ITextInputTarget;

//テキストの入力を行うウィジェットクラス(オンスクリーンキーボードのキー盤)
//入力したテキストを簡便に取得するために必要
class ITextInputWidget {
    public:
        //変換中の読みがあれば、それもカーソル位置へ挟んだ形で返す
        virtual FixedString<PICO_STR_LL> getText() = 0;
        //テキストを差し替える。カーソルは末尾へ置かれ、変換中の読みは捨てられる
        virtual void setText(const FixedString<PICO_STR_LL>& text) = 0;

        //getText()上のカーソルのバイト位置
        virtual size_t getCursorByteOffset() = 0;
        //getText()上のバイト位置へカーソルを置く(変換中の読みがあれば先に確定させる)
        virtual void setCursorByteOffset(size_t byte_offset) = 0;
        //変換中の読みのgetText()上の範囲(バイト)。読みが無ければlen=0
        virtual void getComposition(size_t& start, size_t& len) { start = 0; len = 0; }

        virtual void setInputTarget(ITextInputTarget* target) = 0;
        virtual void removeInputTarget(ITextInputTarget* valid_target) = 0;
        virtual ITextInputTarget* getInputTarget() = 0;
};

//テキストの入力を受けるターゲットクラス
//継承して使うべし
//キーボードが入力のイベントを伝搬するために必要
//KeyboardFunctions::Show()(またはRegister/Unregister関数)を使うべし
class ITextInputTarget {
    public:
        //キーボードが開いた。ここでkeyboard->setText()して編集対象を渡す
        virtual void onShow(ITextInputWidget* keyboard) = 0;
        //テキストが変わった(カーソル移動だけのときは呼ばれない)
        virtual void onTextChanged(ITextInputWidget* keyboard) = 0;
        //キーボードが閉じた(入力の確定)
        virtual void onHide(ITextInputWidget* keyboard) = 0;

        virtual bool getIsSingleLine() = 0;
        virtual void setIsSingleLine(bool is_single_line) = 0;

        //---- 以下は任意(画面下に据え置いて直接編集するターゲット向け) ----

        //テキスト・カーソル・変換中の読みのどれかが変わるたびに呼ばれる
        virtual void onDisplayChanged(ITextInputWidget* keyboard) {}
        //カーソルが先頭にあるときに1文字削除が押された。処理したらtrue
        //(複数行を1行ずつキーボードへ渡している編集欄が、前の行と繋げるために使う)
        virtual bool onBackspaceAtStart(ITextInputWidget* keyboard) { return false; }
        //カーソルが端にあってそれ以上動かせない(dir: -1=左端で←, +1=右端で→)。処理したらtrue
        virtual bool onCursorAtEdge(ITextInputWidget* keyboard, int dir) { return false; }
        //カーソルが末尾にあるときに物理キーボードのDeleteが押された。処理したらtrue
        //(onBackspaceAtStart()の逆向き。次の行と繋げるために使う)
        virtual bool onDeleteAtEnd(ITextInputWidget* keyboard) { return false; }

        virtual ~ITextInputTarget() = default;
};
