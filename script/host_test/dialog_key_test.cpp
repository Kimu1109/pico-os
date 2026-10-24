// ダイアログを物理キーボードで最初から操作できること(KeyInput_Dispatch.cpp + FocusFunctions + 各ダイアログ)。
//
// 確かめること:
//   - ダイアログが開くとフォーカスが中へ入る(InputDialogは入力欄)。キーで操作していれば枠も出す、タッチなら出さない
//   - フォーカスのある入力欄へ打つと、キーボードを開いてその文字が入る(先にEnterを押さなくてよい)
//   - 1行の入力欄でEnter → キーボードが閉じてダイアログが「決定」で閉じる。Escはキャンセル
//   - MsgDialog: Escはキャンセル、ボタンがOKだけならOK。PickerDialog/FileSaveDialogの最初のフォーカス
//   - ダイアログを開いている間、画面のonKey()へはメディアキーしか届かない
//   - 閉じたら開く前のものへフォーカスが戻る
//   - ColorDialog・MonthGrid・DurationPicker(直接描く部品)の矢印と決定
#include "system/KeyInput_Functions.hpp"
#include "gui/core/Focus_Functions.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Keyboard_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "system/Log_Functions.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/keyboards/KeyboardEng.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}

// ---- キーボードの窓口の偽物: Show()で英字のキー盤を1つ開く ----
static KeyboardEng* g_eng = nullptr;
static KeyboardPanel* g_visible = nullptr;
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget* t){
    if(g_eng && g_eng->getInputTarget() == t) g_eng->removeInputTarget(t);
}
void KeyboardFunctions::Show(ITextInputTarget* t, KeyboardFunctions::Layout, bool){
    g_eng->setInputTarget(t);
    g_eng->setVisible(true);
}
void KeyboardFunctions::HideAll(){ if(g_visible) g_visible->setVisible(false); }
void KeyboardFunctions::SwitchPanel(KeyboardPanel*, KeyboardPanel*){}
bool KeyboardFunctions::ToggleJapanese(){ return false; }
void KeyboardFunctions::OnPanelShown(KeyboardPanel* p){ g_visible = p; }
void KeyboardFunctions::OnPanelHidden(KeyboardPanel* p){ if(g_visible == p) g_visible = nullptr; }
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel* panel, bool text_changed){
    ITextInputTarget* t = panel->getInputTarget();
    if(t && text_changed && panel->getVisible()) t->onTextChanged(panel);
}
KeyboardPanel* KeyboardFunctions::VisiblePanel(){ return g_visible; }

// ---- 画面の偽物: 届いた打鍵を数えて全部取る ----
class FakeScene : public Scene {
    public:
        int keys = 0;
        int media = 0;
        const char* getName() const override { return "fake"; }
        void onEnter() override {}
        bool onKey(const KeyInputFunctions::Event& ev) override {
            if(ev.key == KeyInputFunctions::Key::PlayPause){ media++; return true; }
            //矢印・Tab・Enter・Escはフォーカスへ回す(普通の画面と同じ)。文字だけ取る
            if(ev.key != KeyInputFunctions::Key::Char) return false;
            keys++;
            return true;
        }
};
static FakeScene g_scene;
Scene* SceneFunctions::Current(){ return &g_scene; }

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

using IK = KeyInputFunctions::Key;
static void Press(IK k, uint32_t cp = 0, uint8_t mods = 0){
    KeyInputFunctions::Event e;
    e.key = k; e.cp = cp; e.mods = mods;
    KeyInputFunctions::Push(e);
    KeyInputFunctions::Update();
}
static void Type(const char* ascii){
    for(const char* p = ascii; *p; p++) Press(IK::Char, (uint8_t)*p);
}
// 何も打たずに1フレーム(開いたダイアログをFocusFunctions::Update()が見る)
static void Frame(){ KeyInputFunctions::Update(); }

template<typename D>
static D* Open(D* d){
    WidgetFunctions::AddDialog(d);
    d->setVisible(true);
    Frame();
    return d;
}

int main(){
    KeyInputFunctions::Setup();
    g_eng = new KeyboardEng();
    g_eng->setVisible(false);

    Button* behind = new Button(10, 30, "behind");
    WidgetFunctions::Add(behind);

    // ---- InputDialog: 開いたら入力欄にフォーカス、打てばキーボードが開いて入る、Enterで決定 ----
    FocusFunctions::key_mode = true;
    FocusFunctions::Set(behind, true);
    int closed = 0; bool closed_ok = false;
    InputDialog* in = Open(new InputDialog("名前:", true));
    in->setOnClosed([&](bool ok){ closed++; closed_ok = ok; });
    check(FocusFunctions::focused && FocusFunctions::focused->takesTypedText() && FocusFunctions::ring_visible,
          "InputDialog: 開くと入力欄にフォーカス(キーで操作中なので枠も)");
    Type("ab");
    check(g_visible == g_eng && g_eng->getText() == "ab", "入力欄へ打つとキーボードが開いて文字が入る");
    check(g_scene.keys == 0, "ダイアログの間は画面のonKey()へ文字が届かない");
    Press(IK::Enter);
    check(g_visible == nullptr && closed == 1 && closed_ok, "Enterでキーボードが閉じ、ダイアログも「決定」で閉じる");
    check(strcmp(in->getInput().c_str(), "ab") == 0, "入力した文字が残っている");
    Frame();
    check(FocusFunctions::focused == behind && FocusFunctions::ring_visible, "閉じたら開く前のものへ戻る");
    WidgetFunctions::Destroy(in);

    // Esc: キーボードが開いていれば閉じるだけ、もう一度でキャンセル
    closed = 0;
    in = Open(new InputDialog("パスワード:", true));
    in->setOnClosed([&](bool ok){ closed++; closed_ok = ok; });
    Type("x");
    Press(IK::Escape);
    check(g_visible == nullptr && closed == 0, "Esc(1回目): キーボードを閉じるだけ");
    Press(IK::Escape);
    check(closed == 1 && !closed_ok, "Esc(2回目): キャンセルで閉じる");
    Frame();
    WidgetFunctions::Destroy(in);

    // 複数行の入力欄: Enterは改行、Ctrl+Enterで決定
    closed = 0;
    in = Open(new InputDialog("メモ:", false));
    in->setOnClosed([&](bool ok){ closed++; closed_ok = ok; });
    Type("a");
    Press(IK::Enter);
    check(g_visible == g_eng && closed == 0, "複数行: Enterは改行(閉じない)");
    Press(IK::Enter, 0, KeyInputFunctions::Ctrl);
    check(closed == 1 && closed_ok, "複数行: Ctrl+Enterで決定");
    Frame();
    WidgetFunctions::Destroy(in);

    // ---- タッチで操作していた: フォーカスは入るが枠は出さない(打てばそのまま入る) ----
    FocusFunctions::OnTouchStart(behind);
    closed = 0;
    in = Open(new InputDialog("名前:", true));
    in->setOnClosed([&](bool ok){ closed++; closed_ok = ok; });
    check(FocusFunctions::focused && FocusFunctions::focused->takesTypedText() && !FocusFunctions::ring_visible,
          "タッチの後に開いたダイアログ: 入力欄にフォーカス、枠は出さない");
    Press(IK::Char, 'z');
    check(g_visible == g_eng && g_eng->getText() == "z", "枠が無くても入力欄へ打てる");
    Press(IK::Enter);
    check(closed == 1 && closed_ok, "Enterで決定");
    Frame();
    WidgetFunctions::Destroy(in);

    // ---- MsgDialog ----
    FocusFunctions::key_mode = true;
    MsgDialog* msg = Open(new MsgDialog("消しますか?", "キャンセル", "消す"));
    int msg_closed = 0; bool msg_ok = true;
    msg->setOnClosed([&](bool ok){ msg_closed++; msg_ok = ok; });
    check(FocusFunctions::focused && FocusFunctions::ring_visible, "MsgDialog: 開くとボタンに枠");
    Press(IK::PlayPause);
    check(g_scene.media == 1, "ダイアログの間もメディアキーは画面へ届く");
    Press(IK::Escape);
    check(msg_closed == 1 && !msg_ok, "MsgDialog: Escはキャンセル");
    Frame();
    WidgetFunctions::Destroy(msg);

    msg = Open(new MsgDialog("保存しました", "", "OK"));
    msg_closed = 0;
    msg->setOnClosed([&](bool ok){ msg_closed++; msg_ok = ok; });
    Press(IK::Escape);
    check(msg_closed == 1 && msg_ok, "MsgDialog: OKだけならEscでOK");
    Frame();
    WidgetFunctions::Destroy(msg);

    msg = Open(new MsgDialog("続けますか?", "いいえ", "はい"));
    msg_closed = 0;
    msg->setOnClosed([&](bool ok){ msg_closed++; msg_ok = ok; });
    Press(IK::Enter);
    check(msg_closed == 1 && msg_ok, "MsgDialog: キーで操作中なら最初のEnterで上のボタン(はい)");
    Frame();
    WidgetFunctions::Destroy(msg);

    // ---- PickerDialog: 中身から始まる ----
    PickerDialog* date = new PickerDialog(PickerDialog::Mode::Date, "日付", "決定", "キャンセル");
    date->setDate(2026, 10, 24);
    date->layout();
    Open(date);
    check(FocusFunctions::focused && FocusFunctions::focused->getWidgetType() == WidgetType::MonthGrid,
          "日付のダイアログ: 最初は日の格子(「<」ボタンではない)");
    Press(IK::Right);
    check(date->getDay() == 25, "MonthGrid: →で次の日");
    Press(IK::Up);
    check(date->getDay() == 18, "MonthGrid: ↑で1週前");
    Press(IK::Down);
    Press(IK::Down);
    check(date->getDay() == 25 && FocusFunctions::focused->getWidgetType() != WidgetType::MonthGrid,
          "MonthGrid: 月の外へ出る向き(25日の1週後)はフォーカスが隣へ(日は変わらない)");
    int picked = 0; bool picked_ok = false;
    date->setOnClosed([&](bool ok){ picked++; picked_ok = ok; });
    Press(IK::Escape);
    check(picked == 1 && !picked_ok, "PickerDialog: Escでキャンセル");
    Frame();
    WidgetFunctions::Destroy(date);

    PickerDialog* time = new PickerDialog(PickerDialog::Mode::Time, "時刻", "決定", "キャンセル");
    time->setTime(10, 30, 0);
    time->layout();
    Open(time);
    check(FocusFunctions::focused && FocusFunctions::focused->getWidgetType() == WidgetType::DurationPicker,
          "時刻のダイアログ: 最初は時:分:秒");
    Press(IK::Up);
    check(time->getTimeSeconds() == 11u * 3600u + 30u * 60u, "DurationPicker: ↑で時を1つ進める");
    Press(IK::Right);
    Press(IK::Down);
    check(time->getTimeSeconds() == 11u * 3600u + 29u * 60u, "DurationPicker: →で分を選び、↓で1つ戻す");
    Frame();
    WidgetFunctions::Destroy(time);

    // ---- ColorDialog ----
    ColorDialog* color = Open(new ColorDialog());
    int color_closed = 0; bool color_ok = false;
    color->setOnClose([&](bool ok){ color_closed++; color_ok = ok; });
    check(FocusFunctions::focused == color, "ColorDialog: 開くと色の格子にフォーカス");
    Press(IK::Right);
    Press(IK::Down);
    check(color->getSelectedColor() == 5, "ColorDialog: 矢印で色を選ぶ(→ ↓ = 5)");
    Press(IK::Enter);
    check(color_closed == 1 && color_ok, "ColorDialog: 決定でOK");
    Frame();
    WidgetFunctions::Destroy(color);

    // ---- FileSaveDialog: ファイル名の欄から始まる ----
    FileSaveDialog* save = Open(new FileSaveDialog("/"));
    int save_closed = 0; bool save_ok = false;
    save->setOnClose([&](bool ok){ save_closed++; save_ok = ok; });
    check(FocusFunctions::focused && FocusFunctions::focused->takesTypedText(), "FileSaveDialog: 最初はファイル名の欄");
    Type("memo.txt");
    Press(IK::Enter);
    check(save_closed == 1 && save_ok, "FileSaveDialog: 名前を打ってEnterで保存(OK)");
    Frame();
    WidgetFunctions::Destroy(save);

    // ---- ダイアログが無ければ画面のonKey()が文字を取る ----
    g_scene.keys = 0;
    Press(IK::Char, 'q');
    check(g_scene.keys == 1, "ダイアログが無ければ画面へ届く");

    WidgetFunctions::ClearSceneWidgets();
    delete g_eng;

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
