#include "functions/Scene_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "functions/Keyboard_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Mem_Functions.hpp"
#include "consts.hpp"

namespace {
    // 現シーンの後片付け。シーンオブジェクト自体のdeleteは呼び出し側の責務
    void TeardownCurrent(){
        if(!SceneFunctions::current) return;

        // キーボードは常駐なので破棄しないが、入力対象(シーン内のTextbox等)が
        // 消える前に閉じてonHide()を届けておく
        KeyboardFunctions::HideAll();

        SceneFunctions::current->onExit();

        // 通常レイヤ・ダイアログ層はシーンの所有物なので一括破棄する
        WidgetFunctions::ClearSceneWidgets();

        //破棄しきった直後のヒープを見て、戻らなかった分をリーク候補として記録する
        MemFunctions::OnSceneExit();
    }

    // 遷移要求を1件だけ受け付ける
    bool AcceptRequest(SceneFunctions::RequestType type, Scene* scene){
        if(SceneFunctions::pending_type != SceneFunctions::RequestType::None){
            // 同一フレーム内の2件目以降は破棄する(1フレーム1遷移)
            LOG_SYS_WARN("シーン遷移要求が重複したため後の要求を破棄しました");
            delete scene;
            return false;
        }
        SceneFunctions::pending_type = type;
        SceneFunctions::pending_scene = scene;
        return true;
    }
}

void SceneFunctions::Setup(Scene* first_scene){
    current = nullptr;
    stack_depth = 0;
    for(int i = 0; i < kMaxSceneDepth; i++) stack[i] = nullptr;
    pending_type = RequestType::None;
    pending_scene = nullptr;

    if(!first_scene){
        LOG_SYS_FAIL("Scene Setup has failed! (first_scene is null)");
        return;
    }

    current = first_scene;
    MemFunctions::BeforeSceneEnter();
    current->onEnter();
    MemFunctions::AfterSceneEnter(current->getName());

    LOG_SYS_OK("Scene Setup has succeeded! (%s)", current->getName());
}

void SceneFunctions::Change(Scene* next){
    if(!next){
        LOG_SYS_WARN("シーン遷移要求が無効です(next is null)");
        return;
    }
    AcceptRequest(RequestType::Change, next);
}

void SceneFunctions::Push(Scene* next){
    if(!next){
        LOG_SYS_WARN("シーン遷移要求が無効です(next is null)");
        return;
    }
    AcceptRequest(RequestType::Push, next);
}

void SceneFunctions::Pop(){
    AcceptRequest(RequestType::Pop, nullptr);
}

bool SceneFunctions::CanPop(){
    return stack_depth > 0;
}

Scene* SceneFunctions::Current(){
    return current;
}

int SceneFunctions::Depth(){
    return stack_depth;
}

void SceneFunctions::Update(){
    if(pending_type != RequestType::None){
        const RequestType type = pending_type;
        Scene* next = pending_scene;
        pending_type = RequestType::None;
        pending_scene = nullptr;

        // 破棄と生成の途中でdirtyRectsがウィジェット数ぶん積み上がるのを防ぐ。
        // 遷移では結局画面全体を描き直すので、最後に全画面1枚だけをdirtyとして登録する
        const bool prev_dirty_deactivates = PICO_GFX::isDirtyDeactivates;
        PICO_GFX::isDirtyDeactivates = true;

        Scene* entering = nullptr;

        switch(type){
            case RequestType::Change: {
                Scene* leaving = current;
                TeardownCurrent();
                current = nullptr;
                delete leaving;
                entering = next;
                break;
            }
            case RequestType::Push: {
                if(stack_depth >= kMaxSceneDepth){
                    LOG_SYS_WARN("シーンスタックが上限(%d)に達したためPushを破棄しました", kMaxSceneDepth);
                    delete next;
                    break;
                }
                Scene* leaving = current;
                TeardownCurrent();
                current = nullptr;
                //シーンオブジェクトは残すがウィジェットは解放済み。Pop()時にonEnter()で作り直す
                if(leaving) stack[stack_depth++] = leaving;
                entering = next;
                break;
            }
            case RequestType::Pop: {
                if(stack_depth <= 0){
                    LOG_SYS_WARN("戻り先のシーンが無いためPopを破棄しました");
                    break;
                }
                Scene* leaving = current;
                TeardownCurrent();
                current = nullptr;
                delete leaving;
                entering = stack[--stack_depth];
                stack[stack_depth] = nullptr;
                break;
            }
            case RequestType::None:
            default:
                break;
        }

        if(entering){
            current = entering;
            //onEnter()を挟んで計測することで「このシーンのウィジェットが要求するバイト数」が取れる
            MemFunctions::BeforeSceneEnter();
            current->onEnter();
            MemFunctions::AfterSceneEnter(current->getName());
            LOG_SYS_MSG("シーン遷移: %s (depth=%d)", current->getName(), stack_depth);
        }

        PICO_GFX::isDirtyDeactivates = prev_dirty_deactivates;

        if(entering){
            PICO_GFX::MarkDirty({0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
        }
    }

    if(current) current->onUpdate();
}
