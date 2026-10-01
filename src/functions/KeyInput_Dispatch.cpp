#include "functions/KeyInput_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "gui/scenes/Scene.hpp"
#include "gui/widgets/keyboards/KeyboardPanel.hpp"

// 打鍵の配り先(KeyInput_Functions.hpp「届け先」)。
// 列と行の読み取りは KeyInput_Functions.cpp(何にも依存しない)

void KeyInputFunctions::Update(){
    bool any = false;
    Event ev;
    while(Pop(ev)){
        any = true;

        //1. 今の画面
        Scene* scene = SceneFunctions::Current();
        if(scene && scene->onKey(ev)) continue;

        //2. 開いているキー盤(画面のonKey()の中で開かれた場合もここで受け取る)
        KeyboardPanel* panel = KeyboardFunctions::VisiblePanel();
        if(panel) panel->onPhysicalKey(ev);

        //3. 誰も取らなければ捨てる
    }
    detail::SetDispatchedThisFrame(any);
}
