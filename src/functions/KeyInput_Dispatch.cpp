#include "functions/KeyInput_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include "functions/Focus_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include <Arduino.h>

// 打鍵の配り先(KeyInput_Functions.hpp「届け先」)。
// 列と行の読み取りは KeyInput_Functions.cpp(何にも依存しない)

namespace {
    // 画面もキー盤も取らなかった打鍵で、ウィジェットのフォーカスを動かす(FocusFunctions)
    bool DispatchFocus(const KeyInputFunctions::Event& ev){
        using KeyInputFunctions::Key;
        if(ev.ctrl() || ev.alt()) return false;
        switch(ev.key){
            case Key::Tab:    return FocusFunctions::Navigate(ev.shift() ? FocusFunctions::Move::Prev : FocusFunctions::Move::Next);
            case Key::Up:     return FocusFunctions::SendKey(FocusKey::Up);
            case Key::Down:   return FocusFunctions::SendKey(FocusKey::Down);
            case Key::Left:   return FocusFunctions::SendKey(FocusKey::Left);
            case Key::Right:  return FocusFunctions::SendKey(FocusKey::Right);
            case Key::Enter:  return FocusFunctions::Activate();
            case Key::Escape: return FocusFunctions::SendKey(FocusKey::Back);
            case Key::Char:   return ev.cp == ' ' ? FocusFunctions::Activate() : false;
            default:          return false;
        }
    }

    // 十字キーを押したままにしたときの繰り返し(最初は少し待つ)
    constexpr uint32_t kPadRepeatDelayMs = 400;
    constexpr uint32_t kPadRepeatMs      = 110;
    uint16_t pad_repeat_button = 0;
    uint32_t pad_repeat_at = 0;

    // コントローラーでフォーカスを動かす。十字=移動、A=決定、B=戻る、L/ZL・R/ZR=前/次。
    // 画面がコントローラーを自分で読む(Scene::usesPad())間と、キー盤が開いている間はしない
    void UpdatePadFocus(Scene* scene, uint32_t now){
        if(!scene || scene->usesPad() || KeyboardFunctions::VisiblePanel() || !PadFunctions::IsConnected()){
            pad_repeat_button = 0;
            return;
        }
        static const struct { uint16_t button; FocusKey key; } kDirs[] = {
            {PadFunctions::Up, FocusKey::Up}, {PadFunctions::Down, FocusKey::Down},
            {PadFunctions::Left, FocusKey::Left}, {PadFunctions::Right, FocusKey::Right},
        };
        for(const auto& d : kDirs){
            if(PadFunctions::Pressed(d.button)){
                FocusFunctions::SendKey(d.key);
                pad_repeat_button = d.button;
                pad_repeat_at = now + kPadRepeatDelayMs;
            }else if(d.button == pad_repeat_button){
                if(!PadFunctions::IsDown(d.button)) pad_repeat_button = 0;
                else if((int32_t)(now - pad_repeat_at) >= 0){
                    FocusFunctions::SendKey(d.key);
                    pad_repeat_at = now + kPadRepeatMs;
                }
            }
        }
        if(PadFunctions::Pressed(PadFunctions::A)) FocusFunctions::Activate();
        if(PadFunctions::Pressed(PadFunctions::B)) FocusFunctions::SendKey(FocusKey::Back);
        if(PadFunctions::Pressed(PadFunctions::L | PadFunctions::ZL)) FocusFunctions::Navigate(FocusFunctions::Move::Prev);
        if(PadFunctions::Pressed(PadFunctions::R | PadFunctions::ZR)) FocusFunctions::Navigate(FocusFunctions::Move::Next);
    }
}

void KeyInputFunctions::Update(){
    bool any = false;
    Event ev;
    while(Pop(ev)){
        any = true;

        //0. 日本語入力の入り切り。キー盤が開いていなければ、Tabだけは普通に配る
        if(CheckImeToggle(ev, (uint32_t)millis())){
            if(KeyboardFunctions::ToggleJapanese()) continue;
            if(ev.key != Key::Tab) continue;
        }

        //日本語の読みを入力中/変換中なら、画面より先にキー盤へ
        KeyboardPanel* panel = KeyboardFunctions::VisiblePanel();
        if(panel && panel->wantsKeyFirst(ev) && panel->onPhysicalKey(ev)) continue;

        //1. 今の画面
        Scene* scene = SceneFunctions::Current();
        if(scene && scene->onKey(ev)) continue;

        //2. 開いているキー盤(画面のonKey()の中で開かれた場合もここで受け取る)
        panel = KeyboardFunctions::VisiblePanel();
        if(panel){
            panel->onPhysicalKey(ev);
            continue;
        }

        //3. ウィジェットのフォーカス(Tab・矢印・Enter・Space・Esc)。それ以外は捨てる
        DispatchFocus(ev);
    }
    detail::SetDispatchedThisFrame(any);

    //外部コントローラーでもフォーカスを動かす
    UpdatePadFocus(SceneFunctions::Current(), (uint32_t)millis());
    FocusFunctions::Update();
}
