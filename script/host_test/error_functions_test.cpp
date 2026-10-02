// ErrorFunctions::ShowFatal()(エラーの見せ方の共通口)を検証するテスト。
//
// 「Luaのエラーをpcallで拾った後に出す先が無い」という穴を塞ぐために追加した
// 関数で、呼ぶとMsgDialogが1枚生成・登録され、閉じると自己破棄することを確認する
// (CLAUDE.md「Lua着手前の受け皿の状態」の「エラーの見せ方」に対応)。
#include "functions/Error_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include <cstdio>

// ---- モック(widget_factory_test.cppと同じ方針) ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    check(WidgetFunctions::dialog_roots.empty(), "前提: 呼び出し前はdialog_rootsが空");

    ErrorFunctions::ShowFatal("テストエラー");

    check(WidgetFunctions::dialog_roots.size() == 1, "ShowFatal: ダイアログが1枚登録される");
    MsgDialog* dialog = static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back());
    check(dialog->getVisible(), "ShowFatal: ダイアログが表示状態になる");
    check(dialog->getVisibleIcon(), "ShowFatal: アイコン表示が有効");
    check(dialog->getIconId() == IconID::AlertTriangle, "ShowFatal: 警告アイコンが使われる");

    // OKを押した扱いにする(causeOnClosedはMsgDialog::setOnClosedへ登録した
    // ラムダを呼ぶだけで、setVisible(false)自体はボタンのコールバック側の仕事なので
    // ここでは呼ばない=causeOnClosedだけを直接叩いて確認する)
    dialog->causeOnClosed(true);
    check(WidgetFunctions::pending_deletes.size() == 1, "閉じるとDestroyLaterで解放待ちに積まれる");

    WidgetFunctions::ProcessPendingDeletes();
    check(WidgetFunctions::dialog_roots.empty(), "解放後はdialog_rootsも空に戻る");

    // 空文字/nullptrでも落ちずに既定のメッセージへフォールバックする
    ErrorFunctions::ShowFatal(nullptr);
    check(WidgetFunctions::dialog_roots.size() == 1, "nullptrでも安全にダイアログが出る");
    static_cast<MsgDialog*>(WidgetFunctions::dialog_roots.back())->causeOnClosed(false);
    WidgetFunctions::ProcessPendingDeletes();

    // ボタンの文字が空ならそのボタンは作らない(子の数で見る。本文の枠とアイコンは常にある)
    {
        auto count_buttons = [](MsgDialog* d){
            int n = 0;
            for(Widget* w : d->getChildren()) if(w->getWidgetType() == WidgetType::Button) n++;
            return n;
        };
        MsgDialog both("本文", "キャンセル", "OK");
        check(count_buttons(&both) == 2, "MsgDialog: 両方の文字があればボタン2つ");
        MsgDialog ok_only("本文", "", "OK");
        check(count_buttons(&ok_only) == 1, "MsgDialog: キャンセルの文字が空ならボタン1つ");
        MsgDialog cancel_only("本文", "閉じる", nullptr);
        check(count_buttons(&cancel_only) == 1, "MsgDialog: OKがnullptrでもボタン1つ");
        MsgDialog none("本文", "", "");
        check(count_buttons(&none) == 1, "MsgDialog: 両方空なら閉じられるようOKだけ出す");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
