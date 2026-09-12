// Labelのテキストレイアウト結果を固定するテスト。
//
// relayout()は段落分割・UTF-8の1文字送り・折り返し判定・マークアップ解釈を
// まとめて行っており、ここを触ると全ウィジェットの文字表示に影響する。
// 内部構造(lines/cursor_slots)は非公開なので、公開APIから観測できる値
// (幅・高さ・文字数・カーソル座標)を突き合わせて挙動を固定する。
//
// 幅はスタブのtextWidth()に依存する: ASCII=フォント高の半分、マルチバイト=フォント高。
// 既定フォントは24pxなので ASCII=12px / 日本語=24px、行高=24px。
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "functions/Font_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::Setup(){}
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void eq(int actual, int expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-52s 実測=%d 期待=%d\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// 行高24px + 装飾余白2px なので、n行のLabelの高さは n*24+2 になる
static int expectH(int lines){ return lines * 24 + 2; }

int main(){
    //OSData::frameはスタブ(stubs/OS_Data.hpp)が用意済み。ここで作り直すと
    //元のインスタンスが到達不能になりLeakSanitizerに拾われる
    FontFn::SetDefault();

    // ---- 1行 ----
    {
        Label<PICO_STR_M> l(0, 0, "pico-os");
        eq(l.getW(), 7 * 12, "ASCII7文字の幅");
        eq(l.getH(), expectH(1), "ASCII7文字の高さ(1行)");
        eq(l.getTextLength(), 7, "ASCII7文字の文字数");
    }

    // ---- 空文字列(1行ぶんの高さを持つ) ----
    {
        Label<PICO_STR_M> l(0, 0, "");
        eq(l.getW(), 0, "空文字列の幅");
        eq(l.getH(), expectH(1), "空文字列の高さ");
        eq(l.getTextLength(), 0, "空文字列の文字数");
    }

    // ---- 日本語(マルチバイトの1文字送り) ----
    {
        Label<PICO_STR_M> l(0, 0, "日本語テスト");
        eq(l.getW(), 6 * 24, "日本語6文字の幅");
        eq(l.getH(), expectH(1), "日本語6文字の高さ");
        eq(l.getTextLength(), 6, "日本語6文字の文字数");
    }

    // ---- 段落分割 ----
    {
        Label<PICO_STR_M> l(0, 0, "あい\nうえお");
        eq(l.getW(), 3 * 24, "2段落の幅(長い方の行)");
        eq(l.getH(), expectH(2), "2段落の高さ");
        eq(l.getTextLength(), 6, "2段落の文字数(改行1つを含む)");
    }
    {
        //末尾が改行の場合、空の段落が1つ増える
        Label<PICO_STR_M> l(0, 0, "あい\n");
        eq(l.getH(), expectH(2), "末尾改行の高さ(空段落を含む)");
        eq(l.getTextLength(), 3, "末尾改行の文字数");
    }
    {
        Label<PICO_STR_M> l(0, 0, "あ\nい\nう");
        eq(l.getH(), expectH(3), "3段落の高さ");
    }

    // ---- 折り返し ----
    {
        //日本語1文字24px、max_width=100なら1行に4文字(96px)まで
        Label<PICO_STR_L> l(0, 0, "あいうえおかきくけこ");
        l.setMaxWidth(100);
        eq(l.getW(), 100, "折返し時の幅はmax_width");
        eq(l.getH(), expectH(3), "日本語10文字/幅100pxで3行");
        eq(l.getTextLength(), 10, "折返しても文字数は変わらない");
    }
    {
        //折り返しと段落分割の併用
        Label<PICO_STR_L> l(0, 0, "あいうえおか\nきく");
        l.setMaxWidth(100);
        eq(l.getH(), expectH(3), "折返し2行+段落1行で3行");
    }

    // ---- マークアップ ----
    {
        //**で囲むと太字。太字は1文字ごとに幅+1される
        Label<PICO_STR_M> l(0, 0, "**あい**");
        eq(l.getW(), 2 * 24 + 1, "太字2文字の幅(太字分+1)");
        eq(l.getTextLength(), 2, "太字の記号は文字数に入らない");
    }
    {
        Label<PICO_STR_M> l(0, 0, "_あい_");
        eq(l.getW(), 2 * 24, "下線2文字の幅");
        eq(l.getTextLength(), 2, "下線の記号は文字数に入らない");
    }
    {
        Label<PICO_STR_M> l(0, 0, "~~あい~~");
        eq(l.getTextLength(), 2, "打消の記号は文字数に入らない");
    }
    {
        Label<PICO_STR_M> l(0, 0, "**あ**い");
        eq(l.getW(), 24 + 1 + 24, "太字と通常が混在した行の幅");
        eq(l.getTextLength(), 2, "混在時の文字数");
    }
    {
        //自動装飾を切ると記号もそのまま文字として扱われる
        Label<PICO_STR_M> l(0, 0, "");
        l.setDisableAutoTextDecoration(true);
        l.setText("**あい**");
        eq(l.getTextLength(), 6, "自動装飾オフなら記号も1文字として数える");
    }

    // ---- カーソル座標 ----
    {
        Label<PICO_STR_M> l(0, 0, "あいう");
        l.setCursorPos(0);
        eq(l.getCursorScreenX(), 0, "カーソル先頭のX");
        l.setCursorPos(2);
        eq(l.getCursorScreenX(), 2 * 24, "カーソル2文字目のX");
        l.setCursorToEnd();
        eq(l.getCursorScreenX(), 3 * 24, "カーソル末尾のX");
        eq(l.getCursorScreenY(), 0, "1行目のカーソルY");
    }
    {
        Label<PICO_STR_L> l(0, 0, "あいうえおか");
        l.setMaxWidth(100);
        l.setCursorToEnd();
        eq(l.getCursorScreenY(), 24, "折返し2行目のカーソルY");
    }

    // ---- setterの順序でレイアウト結果が変わらないこと ----
    // (レイアウトを遅延させているため、setterをどの順で呼んでも
    //  最後に解決した結果が一致していなければならない)
    {
        Label<PICO_STR_L> a(0, 0, "");
        a.setMaxWidth(100);
        a.setFontSize(FontFn::Normal);
        a.setText("あいうえおかきくけこ");

        Label<PICO_STR_L> b(0, 0, "あいうえおかきくけこ");
        b.setFontSize(FontFn::Normal);
        b.setMaxWidth(100);

        eq(a.getH(), b.getH(), "setterの順序が違っても高さは同じ");
        eq(a.getW(), b.getW(), "setterの順序が違っても幅は同じ");
        eq(a.getTextLength(), b.getTextLength(), "setterの順序が違っても文字数は同じ");
    }

    // ---- 再設定で前の状態が残らないこと ----
    {
        Label<PICO_STR_L> l(0, 0, "あいうえおかきくけこ");
        l.setMaxWidth(100);
        eq(l.getH(), expectH(3), "折返しあり(再設定前)");
        l.setText("あ");
        eq(l.getH(), expectH(1), "短いテキストへ再設定すると1行に戻る");
        eq(l.getTextLength(), 1, "再設定後の文字数");
    }

    // ---- フォントサイズ ----
    {
        //Smallは16px。ASCIIは8px、日本語は16px
        Label<PICO_STR_M> l(0, 0, "あい");
        l.setFontSize(FontFn::Small);
        eq(l.getW(), 2 * 16, "Smallフォントの幅");
        eq(l.getH(), 16 + 2, "Smallフォントの高さ");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
