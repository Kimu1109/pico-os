#include "services/PowerMenu_Functions.hpp"
#include "services/Power_Functions.hpp"
#include "services/Lock_Functions.hpp"
#include "devtools/CrashDump_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"

namespace {
    // 開いているメニュー(シーンの切り替えでダイアログごと消えることがあるのでIDで持つ)
    WidgetId open_menu = WidgetIdTools::Invalid();
    bool reboot_pending = false;
}

void PowerMenuFunctions::Open(){
    if(WidgetRegistry::Resolve(open_menu)) return;

    auto* d = new PickerDialog(PickerDialog::Mode::Choice, "電源", "", "キャンセル");
    if(!d) return;
    d->addChoice("スリープ");
    d->addChoice("再起動");
    if(LockFunctions::HasPin()) d->addChoice("ロック");
    d->layout();
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    open_menu = d->getId();
    d->setOnClosed([d](bool is_ok){
        if(is_ok){
            switch(d->getSelectedIndex()){
                case 0: PowerFunctions::SleepNow(); break;
                case 1: RequestReboot(); break;
                case 2: LockFunctions::LockNow(); break;
                default: break;
            }
        }
        open_menu = WidgetIdTools::Invalid();
        WidgetFunctions::DestroyLater(d);
    });
}

void PowerMenuFunctions::RequestReboot(){ reboot_pending = true; }

void PowerMenuFunctions::Update(){
    if(!reboot_pending) return;
    reboot_pending = false;

    LOG_SYS_MSG("電源メニュー: 再起動します");
    //今の画面に後片付けをさせる(編集中のアラームの保存・ゲームボーイのセーブ・接続を閉じる等)。
    //上に積まれて隠れている画面は、隠れたときに onExit() 済み
    if(Scene* cur = SceneFunctions::Current()) cur->onExit();
    NotificationFunctions::SaveNow();
    LogFunctions::Flush();
    CrashDumpFunctions::Reboot();
}
