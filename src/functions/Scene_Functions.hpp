#pragma once

#include "gui/scenes/Scene.hpp"
#include <cstdint>

// シーン(画面)の切り替えを司るサブシステム。
//
// 遷移要求(Change/Push/Pop)は即時実行せず、SceneFunctions::Update()が呼ばれる
// フレーム境界まで保留する。ボタンのコールバック内からChange()を呼ぶと、
// そのコールバックを実行中のウィジェット自身をdeleteすることになるため。
// (WidgetFunctions::DestroyLater()と同じ発想)
namespace SceneFunctions {
    // Pop()で戻れる深さの上限。RAM節約のため固定長配列で持つ
    constexpr int kMaxSceneDepth = 4;

    enum class RequestType : uint8_t {
        None,
        Change, // 現シーンを破棄して置き換える(スタックは変化しない)
        Push,   // 現シーンをスタックへ退避して新シーンへ進む(Pop()で戻れる)
        Pop     // 現シーンを破棄して一つ前のシーンへ戻る
    };

    inline Scene* current = nullptr;
    inline Scene* stack[kMaxSceneDepth] = {};
    inline int stack_depth = 0;

    inline RequestType pending_type = RequestType::None;
    inline Scene* pending_scene = nullptr;

    // 起動時に最初のシーンへ入る。first_sceneの所有権はSceneFunctionsが持つ
    void Setup(Scene* first_scene);

    // 以下3つはいずれも「要求の登録」のみを行い、実際の遷移はUpdate()で起こる。
    // 渡したシーンの所有権はSceneFunctionsへ移る(要求が却下された場合はここでdeleteされる)
    void Change(Scene* next);
    void Push(Scene* next);
    void Pop();

    bool CanPop();
    Scene* Current();
    int Depth();

    // 毎フレーム、WidgetFunctions::UpdateAll()より前に呼ぶ。
    // 保留中の遷移をここで実行し、続けて現シーンのonUpdate()を回す
    void Update();
}
