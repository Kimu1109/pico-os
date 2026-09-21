#include "gui/scenes/LuaScene.hpp"
#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Error_Functions.hpp"

#include "Arduino.h"

void LuaScene::onEnter() {
    // 通常はonExit()で必ずnullptrへ戻るが、念のための保険(前回の後始末漏れがあっても
    // 二重確保のままにしない)
    if (engine) {
        delete engine;
        engine = nullptr;
    }

    script_ok = false;

    engine = new LuaEngine(kLuaBudgetBytes);
    if (!engine || !engine->valid()) {
        // LuaEngineのコンストラクタ内で既にLOG_APP_FAILは出ている。
        // 画面側にも見える形で伝える
        ErrorFunctions::ShowFatal("Luaの初期化に失敗しました(メモリ不足の可能性があります)");
        return;
    }

    script_ok = loadAndRun();
    if (script_ok) {
        engine->CallSetup();
    }

    last_tick_ms = millis();
}

bool LuaScene::loadAndRun() {
    FsFile f = OSData::SD.open(script_path.c_str());
    if (!f) {
        ErrorFunctions::ShowFatal("スクリプトを開けません(パスを確認してください)");
        return false;
    }

    const size_t file_size = f.fileSize();
    size_t size = file_size;
    if (size > kMaxScriptBytes) {
        size = kMaxScriptBytes;
        LOG_SYS_WARN("LuaScene: %s が上限(%uB)を超えているため%uBで打ち切りました",
            script_path.c_str(), (unsigned)kMaxScriptBytes, (unsigned)file_size);
    }

    // MarkdownView::load()と同じ理由でスタック上の小さなチャンクで読み進める
    // (ファイル全体ぶんの一時バッファをヒープへ一度に要求すると断片化の原因になるため)
    script_source.clear();
    char chunk[256];
    size_t remaining = size;
    while (remaining > 0) {
        const size_t want = (remaining < sizeof(chunk)) ? remaining : sizeof(chunk);
        const int got = f.read((uint8_t*)chunk, want);
        if (got <= 0) break; // 読み取り失敗。読めたところまでで打ち切る
        script_source.append(chunk, (size_t)got);
        remaining -= (size_t)got;
    }
    f.close();

    return engine->Run(script_source.c_str(), script_path.c_str());
}

void LuaScene::onUpdate() {
    if (!engine || !script_ok) return;

    // 進行中のpico.http_request()を1フレーム分進める。setup()/loop()の有無に
    // 関わらず毎フレーム呼ぶ(HttpRequestはPICO_Taskの全体リストに乗らず、
    // 所有側が自分でupdate()する設計のため。LuaEngineクラスコメント「ネットワーク」参照)
    engine->UpdateHttp();

    const unsigned long now = millis();
    // 符号なしの引き算なのでmillis()の一周(約49日)をまたいでも正しい差になる
    const unsigned long dt = now - last_tick_ms;
    last_tick_ms = now;

    engine->CallLoop((uint32_t)dt);
}

void LuaScene::onExit() {
    // Push()で背後へ退避される場合もonExit()は呼ばれる(Scene.hppのレイヤ説明参照)。
    // Lua stateはウィジェットと同じく「シーンがアクティブな間だけ」の寿命にし、
    // Pop()で戻ってきた際はonEnter()でスクリプトを読み直して最初から実行し直す
    // (他のシーンがonExit()で状態をメンバへ退避してonEnter()で復元するのと違い、
    // Luaアプリの状態はスクリプト内のLua変数にあるため、C++側で退避しようがない)
    delete engine;
    engine = nullptr;
}
