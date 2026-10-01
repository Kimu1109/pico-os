// 物理キーボードの窓口(KeyInputFunctions)の検証。
//
// 確かめること:
//   - "key M CODE" の読み取り(文字・名前のあるキー・修飾キー・壊れた行)
//   - USBシリアル(PadFunctions が読む)の中で "pad" の行と "key" の行が混ざっても両方届く
//   - 列の溢れ・UTF-8への変換
//   - 配り先: 画面の onKey() が先、取らなければ開いているキー盤(英字/数字)へ入る
//   - キー盤の編集(挿入・Backspace・Delete・←→・Home/End・Enterで決定/改行・Esc・数字のキー盤の制限)
#include "functions/KeyInput_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "gui/widgets/keyboards/KeyboardEng.hpp"
#include "gui/widgets/keyboards/KeyboardNum.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}

// ---- キーボードの窓口の偽物: 表示中のキー盤を1つだけ持つ ----
static KeyboardPanel* g_visible = nullptr;
static int g_switch_count = 0;
void KeyboardFunctions::RegisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::UnregisterInputTarget(ITextInputTarget*){}
void KeyboardFunctions::Show(ITextInputTarget*, KeyboardFunctions::Layout, bool){}
void KeyboardFunctions::HideAll(){}
void KeyboardFunctions::SwitchPanel(KeyboardPanel*, KeyboardPanel*){ g_switch_count++; }
void KeyboardFunctions::OnPanelShown(KeyboardPanel* p){ g_visible = p; }
void KeyboardFunctions::OnPanelHidden(KeyboardPanel* p){ if(g_visible == p) g_visible = nullptr; }
void KeyboardFunctions::OnPanelResized(KeyboardPanel*){}
void KeyboardFunctions::OnPanelChanged(KeyboardPanel* panel, bool text_changed){
    if(!panel->getVisible()) return;
    ITextInputTarget* t = panel->getInputTarget();
    if(!t) return;
    if(text_changed) t->onTextChanged(panel);
    if(panel->getVisible()) t->onDisplayChanged(panel);
}
KeyboardPanel* KeyboardFunctions::VisiblePanel(){ return g_visible; }

// ---- 画面の偽物 ----
static Scene* g_scene = nullptr;
Scene* SceneFunctions::Current(){ return g_scene; }

class FakeScene : public Scene {
    public:
        int got = 0;
        bool take_up_down = true;
        const char* getName() const override { return "fake"; }
        void onEnter() override {}
        bool onKey(const KeyInputFunctions::Event& ev) override {
            if(take_up_down && (ev.key == KeyInputFunctions::Key::Up || ev.key == KeyInputFunctions::Key::Down)){
                got++;
                return true;
            }
            return false;
        }
};

class FakeTarget : public ITextInputTarget {
    public:
        bool single = true;
        int hides = 0;
        int delete_at_end = 0;
        int bs_at_start = 0;
        std::string last;
        void onShow(ITextInputWidget* kb) override { kb->setText(FixedString<PICO_STR_LL>("")); }
        void onTextChanged(ITextInputWidget* kb) override { last = kb->getText().c_str(); }
        void onHide(ITextInputWidget* kb) override { hides++; last = kb->getText().c_str(); }
        bool getIsSingleLine() override { return single; }
        void setIsSingleLine(bool s) override { single = s; }
        bool onDeleteAtEnd(ITextInputWidget*) override { delete_at_end++; return true; }
        bool onBackspaceAtStart(ITextInputWidget*) override { bs_at_start++; return true; }
};

using namespace KeyInputFunctions;
using IK = KeyInputFunctions::Key; // KeyboardEng.hppの::Keyと紛れないように

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static bool ParseChar(const char* s, uint32_t cp, uint8_t mods){
    Event e;
    return ParseLine(s, e) && e.key == IK::Char && e.cp == cp && e.mods == mods;
}
static bool ParseKey(const char* s, IK k, uint8_t mods){
    Event e;
    return ParseLine(s, e) && e.key == k && e.mods == mods;
}
static bool Rejects(const char* s){
    Event e;
    return !ParseLine(s, e);
}

static Event Ch(uint32_t cp, uint8_t mods = 0){ Event e; e.key = IK::Char; e.cp = cp; e.mods = mods; return e; }
static Event K(IK k, uint8_t mods = 0){ Event e; e.key = k; e.mods = mods; return e; }

static void TypeAll(const char* ascii){
    for(const char* p = ascii; *p; p++) Push(Ch((uint8_t)*p));
    Update();
}

int main(){
    // ---- 行の読み取り ----
    check(ParseChar("key 0 u+61", 'a', 0), "文字");
    check(ParseChar("key 0 U+3042", 0x3042, 0), "日本語の文字(大文字のU+)");
    check(ParseChar("key 1 u+63", 'c', Ctrl), "Ctrl+C");
    check(ParseChar("key 7 u+1F600", 0x1F600, Ctrl | Alt | Shift), "修飾キー全部・4バイトの文字");
    check(ParseChar("key  0   u+20  ", ' ', 0), "前後の空白");
    check(ParseKey("key 0 enter", IK::Enter, 0), "enter");
    check(ParseKey("key 4 tab", IK::Tab, Shift), "Shift+Tab");
    check(ParseKey("key 0 pagedown", IK::PageDown, 0), "pagedown");
    check(ParseKey("key 2 left", IK::Left, Alt), "Alt+←");
    check(Rejects("key"), "空");
    check(Rejects("key 0"), "CODEが無い");
    check(Rejects("key 8 u+61"), "修飾キーの範囲外");
    check(Rejects("key 10 u+61"), "修飾キーは1桁");
    check(Rejects("key 0 u+"), "符号位置が空");
    check(Rejects("key 0 u+110000"), "Unicodeの範囲外");
    check(Rejects("key 0 u+d800"), "サロゲートは文字ではない");
    check(Rejects("key 0 u+1234567"), "7桁");
    check(Rejects("key 0 enterx"), "知らない名前");
    check(Rejects("key 0 a"), "u+の無い文字");
    check(Rejects("key 0 u+61 x"), "後ろにごみ");
    check(Rejects("pad 10"), "padの行はこちらでは読まない");

    // ---- UTF-8 ----
    {
        char b[5];
        check(EncodeUtf8('A', b) == 1 && strcmp(b, "A") == 0, "UTF-8: 1バイト");
        check(EncodeUtf8(0xE9, b) == 2 && strcmp(b, "\xC3\xA9") == 0, "UTF-8: 2バイト");
        check(EncodeUtf8(0x3042, b) == 3 && strcmp(b, "\xE3\x81\x82") == 0, "UTF-8: 3バイト");
        check(EncodeUtf8(0x1F600, b) == 4 && strcmp(b, "\xF0\x9F\x98\x80") == 0, "UTF-8: 4バイト");
        check(EncodeUtf8(0xD800, b) == 0, "UTF-8: サロゲートは表せない");
    }

    // ---- 列 ----
    Setup();
    for(size_t i = 0; i < kQueueSize + 3; i++) Push(Ch('x'));
    check(Pending() == kQueueSize && DroppedCount() == 3, "溢れた分は捨てて数える");
    DiscardPending();
    check(Pending() == 0, "DiscardPending()で空になる");

    // ---- USBシリアル: padとkeyの行が混ざってもよい ----
    Setup();
    PadFunctions::Setup();
    HostSerial::Feed("key 0 u+68\npad 0010\nkey 1 u+63\ngarbage\nkey 0 bogus\nkey 0 enter\n");
    PadFunctions::UpdateAt(1000);
    check(PadFunctions::IsDown(PadFunctions::A), "padの行はコントローラーへ");
    check(Pending() == 3, "keyの行は列へ(壊れたkeyの行は捨てる)");
    {
        Event e;
        Pop(e); check(e.key == IK::Char && e.cp == 'h', "1つ目: h");
        Pop(e); check(e.key == IK::Char && e.cp == 'c' && e.ctrl(), "2つ目: Ctrl+C");
        Pop(e); check(e.key == IK::Enter, "3つ目: Enter");
    }

    // ---- 配り先と英字のキー盤 ----
    KeyboardEng* eng = new KeyboardEng();
    FakeTarget target;
    FakeScene scene;
    g_scene = &scene;
    eng->setInputTarget(&target);
    eng->setVisible(true);
    check(g_visible == eng, "英字のキー盤が開いた");

    Setup();
    Update();
    check(!HadInputThisFrame(), "打鍵が無ければHadInputThisFrame()は偽");
    TypeAll("hello");
    check(HadInputThisFrame(), "配ったフレームはHadInputThisFrame()が真");
    check(strcmp(eng->getText().c_str(), "hello") == 0, "文字がキー盤へ入る");

    Push(K(IK::Left)); Push(K(IK::Left)); Push(Ch('X')); Update();
    check(strcmp(eng->getText().c_str(), "helXlo") == 0, "←で戻ってカーソル位置へ入る");
    Push(K(IK::Backspace)); Update();
    check(strcmp(eng->getText().c_str(), "hello") == 0, "Backspaceはカーソルの前を消す");
    Push(K(IK::Delete)); Update();
    check(strcmp(eng->getText().c_str(), "helo") == 0, "Deleteはカーソルの後ろを消す");
    Push(K(IK::Home)); Push(Ch(0x3042)); Update();
    check(strcmp(eng->getText().c_str(), "\xE3\x81\x82helo") == 0, "Homeで先頭へ・日本語の文字も入る");
    Push(K(IK::End)); Push(K(IK::Delete)); Update();
    check(target.delete_at_end == 1, "末尾のDeleteは入力先へ知らせる");
    Push(K(IK::Home)); Push(K(IK::Backspace)); Update();
    check(target.bs_at_start == 1, "先頭のBackspaceは入力先へ知らせる");

    Push(Ch('c', Ctrl)); Push(K(IK::Tab)); Update();
    check(strcmp(eng->getText().c_str(), "\xE3\x81\x82helo") == 0, "Ctrl付きの文字とTabはキー盤では扱わない");

    scene.got = 0;
    Push(K(IK::Up)); Push(K(IK::Down)); Update();
    check(scene.got == 2, "画面がonKey()で取ったキーは画面へ");
    scene.take_up_down = false;

    //複数行: Enterは改行、Ctrl+Enterで決定
    target.single = false;
    Push(K(IK::End)); Push(K(IK::Enter)); Update();
    check(strcmp(eng->getText().c_str(), "\xE3\x81\x82helo\n") == 0, "複数行のEnterは改行");
    check(eng->getVisible(), "改行では閉じない");
    Push(K(IK::Enter, Ctrl)); Update();
    check(!eng->getVisible() && target.hides == 1, "Ctrl+Enterで決定して閉じる");

    //閉じた後は誰も取らない(捨てる)
    Push(Ch('z')); Update();
    check(target.hides == 1, "キー盤が無ければ捨てる");

    //単一行: Enterで決定 / Escでも閉じる
    target.single = true;
    eng->setVisible(true);
    TypeAll("ok");
    Push(K(IK::Enter)); Update();
    check(!eng->getVisible() && target.hides == 2 && target.last == "ok", "単一行のEnterで決定");
    eng->setVisible(true);
    Push(K(IK::Escape)); Update();
    check(!eng->getVisible() && target.hides == 3, "Escで閉じる");

    //キー盤の中で1つ目の打鍵が閉じても、残りは安全に捨てられる
    eng->setVisible(true);
    Push(K(IK::Enter)); Push(Ch('q')); Update();
    check(!eng->getVisible(), "閉じた後の打鍵で落ちない");

    // ---- 数字のキー盤: 入れられる文字だけ ----
    KeyboardNum* num = new KeyboardNum(KeyboardNum::MODE_DIGIT | KeyboardNum::MODE_ARITH);
    FakeTarget nt;
    num->setInputTarget(&nt);
    num->setVisible(true);
    TypeAll("1a2*3/4.5(");
    check(strcmp(num->getText().c_str(), "12\xC3\x97" "3\xC3\xB7" "4.5(") == 0, "数字のキー盤: 英字は断り、*→× /→÷");
    num->setAllowedModes(KeyboardNum::MODE_DIGIT);
    TypeAll("+9");
    check(strcmp(num->getText().c_str(), "12\xC3\x97" "3\xC3\xB7" "4.5(9") == 0, "使えないタブの記号は入らない");
    num->setVisible(false);

    delete num;
    delete eng;
    g_scene = nullptr;

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
