#include "functions/Error_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"

void ErrorFunctions::ShowFatal(const char* message) {
    const char* text = (message && *message) ? message : "エラーが発生しました";

    LOG_APP_FAIL("%s", text);

    // FileExplorer::on_press_delete()等と同じ「生成→AddDialog→setVisible→
    // setOnClosedでDestroyLater」の作法。OK/キャンセルどちらを押しても
    // 単に閉じるだけなので、両ボタンとも同じ扱いにしている
    MsgDialog* dialog = new MsgDialog(text, "閉じる", "OK");
    if (!dialog) return; // Widget::operator newの確保失敗(nullptr)。ログは既に出た上で諦める

    dialog->setVisibleIcon(true);
    dialog->setIconId(IconID::AlertTriangle);
    WidgetFunctions::AddDialog(dialog);
    dialog->setVisible(true);
    dialog->setOnClosed([dialog](bool){
        WidgetFunctions::DestroyLater(dialog);
    });
}
