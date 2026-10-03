#include "functions/KeyInput_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"
#include <Arduino.h>

// 打鍵の配り先(KeyInput_Functions.hpp「届け先」)。
// 列と行の読み取りは KeyInput_Functions.cpp(何にも依存しない)

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
        if(panel) panel->onPhysicalKey(ev);

        //3. 誰も取らなければ捨てる
    }
    detail::SetDispatchedThisFrame(any);
}
