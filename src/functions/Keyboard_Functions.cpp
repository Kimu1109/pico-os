#include "functions/Keyboard_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "gui/widgets/keyboards/Keyboard.hpp"
#include "gui/widgets/keyboards/KeyboardEng.hpp"
#include "gui/widgets/keyboards/KeyboardNum.hpp"
#include "gui/widgets/dialogs/KeyboardDialog.hpp"

namespace {
    KeyboardDialog* dialog = nullptr;
    bool docked = false;

    KeyboardPanel* panels[3] = { nullptr, nullptr, nullptr };

    KeyboardPanel* visiblePanel(){
        for(KeyboardPanel* p : panels){
            if(p && p->getVisible()) return p;
        }
        return nullptr;
    }

    KeyboardPanel* panelFor(KeyboardFunctions::Layout layout){
        switch(layout){
            case KeyboardFunctions::Layout::English: return static_cast<KeyboardPanel*>(OSData::keyboard_eng);
            case KeyboardFunctions::Layout::Number:  return static_cast<KeyboardPanel*>(OSData::keyboard_num);
            case KeyboardFunctions::Layout::Japanese:
            default:                                 return static_cast<KeyboardPanel*>(OSData::keyboard_jpn);
        }
    }
}

void KeyboardFunctions::Setup(){
    //ダイアログ枠を先に積み、キー盤をその上へ重ねる(オーバーレイは後に積んだものが手前)
    dialog = new KeyboardDialog();
    WidgetFunctions::AddOverlay(dialog);

    OSData::keyboard_eng = new KeyboardEng();
    OSData::keyboard_jpn = new Keyboard();
    OSData::keyboard_num = new KeyboardNum();
    panels[0] = static_cast<KeyboardPanel*>(OSData::keyboard_eng);
    panels[1] = static_cast<KeyboardPanel*>(OSData::keyboard_jpn);
    panels[2] = static_cast<KeyboardPanel*>(OSData::keyboard_num);
    WidgetFunctions::AddOverlay(OSData::keyboard_eng);
    WidgetFunctions::AddOverlay(OSData::keyboard_jpn);
    WidgetFunctions::AddOverlay(OSData::keyboard_num);

    LOG_SYS_OK("Keyboard Setup has succeeded!");
}

void KeyboardFunctions::Show(ITextInputTarget* target, Layout layout, bool is_docked){
    HideAll();
    RegisterInputTarget(target);
    docked = is_docked;
    panelFor(layout)->setVisible(true);
}

void KeyboardFunctions::RegisterInputTarget(ITextInputTarget *target){
    for(KeyboardPanel* p : panels){
        if(p) p->setInputTarget(target);
    }
}

void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget *target){
    for(KeyboardPanel* p : panels){
        if(p) p->removeInputTarget(target);
    }
}

void KeyboardFunctions::HideAll(){
    for(KeyboardPanel* p : panels){
        if(p && p->getVisible()){
            p->setVisible(false); //内部でtarget->onHide()が呼ばれる
        }
    }
}

bool KeyboardFunctions::IsVisible(){
    return visiblePanel() != nullptr;
}

bool KeyboardFunctions::IsDocked(){
    return docked && IsVisible();
}

KeyboardPanel* KeyboardFunctions::VisiblePanel(){
    return visiblePanel();
}

int KeyboardFunctions::VisibleTop(){
    KeyboardPanel* p = visiblePanel();
    return p ? p->getPanelTop() : SCREEN_HEIGHT;
}

void KeyboardFunctions::SwitchPanel(KeyboardPanel* from, KeyboardPanel* to){
    if(!from || !to || from == to) return;

    const FixedString<PICO_STR_LL> text = from->getText(); //変換中の読みはそのまま確定扱いで引き継ぐ
    const size_t cursor = from->getCursorByteOffset();

    from->setShownSilently(false);
    to->setShownSilently(true);
    to->setText(text);
    to->setCursorByteOffset(cursor);

    if(dialog && !docked) dialog->attach(to);
    OnPanelChanged(to, false);
}

bool KeyboardFunctions::ToggleJapanese(){
    KeyboardPanel* p = visiblePanel();
    KeyboardPanel* jpn = panelFor(Layout::Japanese);
    KeyboardPanel* eng = panelFor(Layout::English);
    if(!p) return false;
    if(p == jpn){ SwitchPanel(jpn, eng); return true; }
    if(p == eng){ SwitchPanel(eng, jpn); return true; }
    return false;
}

void KeyboardFunctions::OnPanelShown(KeyboardPanel* panel){
    //別のキー盤が開いていたら閉じる(targetへはonHideが届く)
    for(KeyboardPanel* p : panels){
        if(p && p != panel && p->getVisible()) p->setVisible(false);
    }
    if(dialog && !docked){
        dialog->attach(panel);
        dialog->setVisible(true);
    }
}

void KeyboardFunctions::OnPanelHidden(KeyboardPanel* panel){
    if(visiblePanel()) return; //切り替え中
    if(dialog && dialog->getVisible()) dialog->setVisible(false);
    docked = false; //次に直接setVisible(true)された場合に備えて既定(ダイアログ)へ戻す
}

void KeyboardFunctions::OnPanelResized(KeyboardPanel* panel){
    if(dialog && !docked && dialog->getVisible()) dialog->attach(panel);
    OnPanelChanged(panel, false);
}

void KeyboardFunctions::OnPanelChanged(KeyboardPanel* panel, bool text_changed){
    if(!panel->getVisible()) return;
    if(dialog && !docked && dialog->getVisible()) dialog->refresh(panel);

    ITextInputTarget* target = panel->getInputTarget();
    if(!target) return;
    if(text_changed) target->onTextChanged(panel);
    //onTextChanged()の中でキーボードが閉じられることがある
    if(panel->getVisible()) target->onDisplayChanged(panel);
}
