#pragma once

#include <cstdint>

#include "gui/scenes/Scene.hpp"
#include "lua/LuaEngine.hpp"
#include "lua/LuaPermissions.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

struct AppEntry;

// SD上のLuaスクリプトを1本読んで実行する画面。
// AppEntry::argへスクリプトパス("/lua/apps/動作確認/main.lua"のようなSD絶対パス)を渡し、
// AppFunctions::MakeSceneWithArg<LuaScene>で登録する(同じLuaScene型を別のargで
// 何個でも登録できるので「Luaスクリプトごとに1タイル」が作れる)。
//
// ウィジェットの生成・削除・イベント配線は全てスクリプト側がpico.*を呼んで行うため、
// このシーン自身が直接newするウィジェットは無い。onEnter/onExitはLuaEngineの
// 生成/破棄と、スクリプトのSDからの読み込みだけを見る。
//
// Arduino風のsetup()/loop(dt)にも対応する。onEnter()でRun()(トップレベルの
// チャンク実行)が成功した場合のみsetup()を1回呼び、以降onUpdate()から毎フレーム
// loop(dt)を呼ぶ(dtは前フレームからの経過ミリ秒)。どちらも定義されていなければ
// 何もしない(必須ではない)。loop()がエラーを出した場合はLuaEngine側の安全弁で
// 以降自動的に呼ばれなくなる(毎フレームのエラーダイアログを防ぐため)。
//
// ランチャへ戻る手段(戻るボタン等)はスクリプト側がpico.create("Button")+
// pico.on(id, "press_start", ...)でpico.pop()を呼ぶ形で自前で用意する
// (ClocksScene/CalculatorScene等、他のアプリの「戻る」ボタンと同じ考え方)。
//
// 本体が16KiBに収まらないアプリ向けに、同じディレクトリの"lib.lua"を伴う「補助モジュール」
// 方式に対応する。存在すれば本体スクリプトより先にrunFile()で読み込み・実行し(本体と同じく
// 浅いコールスタックのうちに1回で済ませる)、結果はスクリプト側がグローバル変数として
// 受け取る(returnではなく`グローバル名 = {...}`の形で書く)。
// 以前はスクリプト自身がpico.sd_read()+Luaの`load()`+`pcall()`でlib.lua相当のファイルを
// 実行時にコンパイルしていたが、この経路は「Luaのバイトコード実行中に、さらにコンパイラの
// 再帰下降パーサーを重ねて呼ぶ」形になり、コア0のスタック(RP2350で4KiBしかない)を
// 実機で実際に使い切ってあふれさせる事故が起きた(ブロック崩し/テトリスの2本で確認)。
// コア0のスタックオーバーフローはコア1(音声専用)のスタックへそのまま溢れ込み、
// 音声が二度と直らない壊れ方をする(SCRATCH_Y/SCRATCH_Xが隣接しているため。詳細は
// このバグの調査記録を参照)。lib.luaをこのクラス側で(本体スクリプトと同じ「トップレベル
// チャンクの実行」として)読み込むことで、スクリプト自身が実行時にLuaコードをコンパイルする
// 経路そのものを無くし、再発を防ぐ。
class LuaScene : public Scene {
    private:
        // このLuaアプリに許すメモリ予算(lua_newstateのカスタムallocへ渡す上限)。
        // CLAUDE.md「RAM/Flash予算」の暫定枠(実機=32bitで200KB)。
        // 64bit の PC/Webビルドでは、同じスクリプトがポインタの大きさの分だけ約1.33倍のメモリを使う
        // (ゾンビTDのモジュールを32bit/64bitのLuaで読み比べた実測: 57KB / 76KB)ので、予算も同じ割合で広げて、
        // 実機で収まるアプリがPCで先にメモリ不足にならないようにする
        static constexpr size_t kLuaBudgetBytes = sizeof(void*) >= 8 ? 200 * 1024 * 4 / 3 : 200 * 1024;

        // スクリプトソースの読み込み上限。MarkdownView::kMdMaxSourceBytes(8KiB)より
        // 大きく取ってある(pico.*呼び出しの羅列でUIを組み立てるスクリプトは
        // Markdown文書より冗長になりがちなため)。超えた分はloadAndRun()が警告して打ち切る。
        // MarkdownViewのdoc_textと同じ理由でメンバ(=ヒープ上のLuaScene本体の一部)に持たせ、
        // スタック上には置かない(16KiBはスタックに置くには大きすぎる)。
        // そのぶんLuaScene自体がMarkdownScene同様「シーン本体は数十バイト」の例外になる
        static constexpr size_t kMaxScriptBytes = PICO_STR_16KiB;
        FixedString<kMaxScriptBytes> script_source;

        FixedString<PICO_PATH_LEN> script_path;

        // このアプリに許す権限(既定は両方false=最小権限)。LuaEngineのapp_dirは
        // ここではなくonEnter()でscript_pathから毎回計算し直す(pico.push_scene/
        // change_sceneで別ファイルへ移った場合、そのファイル自身の親ディレクトリを
        // 見るのが正しいため。CLAUDE.md「Luaバインディング」「権限」参照)
        LuaPermissions permissions;

        uint32_t inherited_rate = 0;    // setInheritedSampleRate()
        LuaEngine* engine = nullptr;

        // ---- 画面をまたぐ受け渡し(pico.push_scene(path, args) / pico.pop(result) / on_suspend・on_resume) ----
        // push_sceneした側が教えてくれた引数(JSON)と、pico.pop(result)の宛先になる親のスクリプト
        FixedString<PICO_STR_1KiB> launch_args;
        FixedString<PICO_PATH_LEN> parent_script;
        // 画面を離れるときの on_suspend() が返した状態(JSON)。次のonEnter()で on_resume(state) に渡す。
        // Pushで背後へ退避されている間もシーンのオブジェクトは残るので、メンバに持てる
        FixedString<PICO_STR_2KiB> saved_state;

        // Run()(トップレベルのチャンク実行)が成功したかどうか。失敗時はsetup()/loop()を
        // 呼ばない(engineの状態が中途半端な可能性があり、追加のエラーダイアログも避けたい)
        bool script_ok = false;

        // loop()へ渡す経過時間(ms)の計算用。ClocksScene等と同じくmillis()の差分で積む
        unsigned long last_tick_ms = 0;

        // 指定パスのLuaファイルをSDから読み込み、engineへ渡して実行する(トップレベルの
        // チャンクとして1回コンパイル+実行するだけ。Run()と同じ「浅いコールスタックのうちに
        // 済ませる」実行になる)。戻り値はRun()の成否。読み込み失敗(ファイルが無い等)や
        // LuaEngine::Run()の失敗はErrorFunctions側で既にダイアログ表示済みなので、
        // ここでは追加のエラー表示をしない
        bool runFile(const char* path);

    public:
        explicit LuaScene(const char* path, const LuaPermissions& permissions = LuaPermissions{})
            : permissions(permissions) {
            script_path.assign(path);
        }

        const char* getName() const override { return "Lua"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
        // 物理キーボードの打鍵をpico.on_key()のコールバックへ渡す(登録が無ければ取らない)
        bool onKey(const KeyInputFunctions::Event& ev) override;
        // pico.pad_* を使ったアプリ(ゲーム)はコントローラーを自分で読む(フォーカスを動かさない)
        bool usesPad() const override;
        // pico.sound_set_rate() で頼まれた音の出力の周波数(0=設定のまま)
        uint32_t preferredSampleRate() const override;
        // push_scene/change_scene で移った先も同じ周波数で鳴らす(同じアプリの中の画面なので)
        void setInheritedSampleRate(uint32_t rate){ inherited_rate = rate; }

        // push_scene/change_sceneの呼び出し側が、引数(JSON。無ければ空)と親のスクリプトパスを教える。
        // 生成直後(Push/Changeする前)に呼ぶ
        void setLaunchArgs(const char* args_json, const char* parent) {
            launch_args.assign(args_json ? args_json : "");
            parent_script.assign(parent ? parent : "");
        }

        // pico.pop(result) が親へ渡す結果の待ち箱。親(target = 親のスクリプトパス)が
        // Pop で戻って onEnter() で受け取る(TakeResult)。短い間(kResultTtlMs)だけ有効で、
        // 誰も受け取らなかった結果が後の別の起動に混ざらない。1件だけ持つ(Popは一度に1画面ぶん)
        static constexpr unsigned long kResultTtlMs = 3000;
        static void PostResult(const char* target, const char* json);
        static bool TakeResult(const char* target, FixedString<PICO_STR_1KiB>& out);

        // テスト・デバッグ用の脱出口(LuaEngine::raw()と同じ位置づけ)。
        // アクティブでない間(onExit()後)はnullptr
        LuaEngine* getEngine() const { return engine; }
};

// AppEntry(name/arg/permissionsを保持する登録簿の1件)からLuaSceneを作る、
// AppFunctions::Register()の第3引数へ渡せる汎用の生成関数。entry.argをスクリプトパス、
// entry.permissionsをそのままLuaSceneへ渡すだけ。静的登録(App_List.cpp)とSDスキャン
// (LuaAppScanner)の両方が共有する。以前はアプリごとに専用の生成関数(MakeLuaHelloScene等)
// を書いて権限を手書きしていたが、AppEntryが権限を持てるようになったことでここへ一本化した
Scene* MakeLuaAppScene(const AppEntry& entry);
