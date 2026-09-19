#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "lua.hpp"
#include "gui/widgets/WidgetID.hpp"

// LuaスクリプトとC++(ウィジェット層)を繋ぐ実行エンジン。1インスタンスが
// 1つのlua_Stateを持つ(1つのLuaアプリ=1つのLuaEngine、という対応を想定)。
//
// 既存の受け皿をそのまま使う薄い橋渡し:
//   - ウィジェットの生成    : WidgetFactory::Create()
//   - ウィジェットの参照    : WidgetRegistry::Resolve()(WidgetIdはLuaへ整数のまま渡す)
//   - プロパティのget/set   : WidgetProperty::Get()/Set()
//   - エラーの見せ方        : ErrorFunctions::ShowFatal()
// このクラス自体が新しく持つのは「pico.*」というLua向けAPI表と、
// タップ等のコールバックをLuaの関数へ中継するための小さな対応表だけ。
//
// メモリ予算: コンストラクタへ渡すbudget_bytesがこのLua state全体(state本体+
// 標準ライブラリ+スクリプト+スクリプトが確保する全テーブル等)の上限になる
// (script/host_test/lua_alloc_budget_test.cppで安全性を検証済み)。
// 超過時はlua_newstate自体がnullptr、あるいはLUA_ERRMEMとして安全に失敗し、
// abort()はしない(luaL_openlibs()を直接pcall無しで呼んだ場合を除く。
// このクラスは内部で必ずpcall越しに呼ぶので安全)。
//
// コールバック(pico.on): Widgetのon_press_start等(std::function<void()>)は
// 引数もコンテキストも持てないが、キャプチャするのは「LuaEngine* + WidgetId」
// (12B程度)だけなのでstd::functionの小バッファに収まりヒープ確保は起きない
// (lua_State*やregistry refをウィジェット側へ持たせない設計にしたため)。
class LuaEngine {
    public:
        // budget_bytes: このLua stateに許す確保量の上限(BudgetAlloc参照)。
        // 構築に失敗した場合(予算不足でstate本体すら作れない等)はvalid()がfalseになる。
        explicit LuaEngine(size_t budget_bytes);
        ~LuaEngine();

        // コピー・ムーブ不可(lua_State*と登録済みコールバックの対応が複雑になるため。
        // 1つのLuaアプリに1インスタンスをnew/deleteする運用を想定)
        LuaEngine(const LuaEngine&) = delete;
        LuaEngine& operator=(const LuaEngine&) = delete;

        bool valid() const { return L != nullptr; }
        size_t usedBytes() const { return used_; }
        size_t budgetBytes() const { return budget_; }

        // 生のlua_State*が要る場面(テスト、将来の高度な相互運用)向けの脱出口。
        // アプリ側のコードは基本的にこれを使わずRun()/pico.*経由で完結させること。
        lua_State* raw() const { return L; }

        // スクリプトを読み込んで即実行する(チャンク名はエラーメッセージにのみ使う)。
        // 構文エラー・実行時エラーはErrorFunctions::ShowFatal()で表示した上でfalseを返す
        // (呼び出し元は追加のエラー表示をしなくてよい)。
        bool Run(const char* script, const char* chunkname = "script");

    private:
        enum class EventKind : uint8_t { PressStart, PressEnd, PressMove, PressOut };

        struct CallbackBinding {
            WidgetId id;
            EventKind kind;
            int ref; // LUA_REGISTRYINDEXに積んだLua関数への参照
        };

        lua_State* L = nullptr;
        size_t budget_;
        size_t used_ = 0;

        std::vector<CallbackBinding> callbacks_;

        static void* Alloc(void* ud, void* ptr, size_t osize, size_t nsize);
        static int InitTrampoline(lua_State* L);

        void registerApi();
        void registerFn(const char* name, lua_CFunction fn);

        // Widgetのコールバックから中継されて呼ばれる(このシグネチャがstd::function<void()>の
        // 小バッファに収まる理由については上のクラスコメント参照)
        void Dispatch(WidgetId id, EventKind kind);
        void BindCallback(class Widget* w, WidgetId id, EventKind kind, int ref);
        void PruneCallbacksFor(WidgetId id);

        static bool EventKindFromName(const char* name, EventKind& out);

        // "pico.*" 関数群。lua_CFunction(引数もコンテキストも持てない素の関数ポインタ)
        // なので、thisはlua_pushcclosureのupvalue経由(lua_upvalueindex(1))で受け取る
        static int l_create(lua_State* L);
        static int l_destroy(lua_State* L);
        static int l_set(lua_State* L);
        static int l_get(lua_State* L);
        static int l_on(lua_State* L);
        static int l_add_child(lua_State* L);
        static int l_log(lua_State* L);
        static int l_show_error(lua_State* L);
        static int l_pop(lua_State* L);
        static int l_content_rect(lua_State* L);
};
