#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "lua.hpp"
#include "gui/widgets/WidgetID.hpp"
#include "consts.hpp"

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
// 直接描画(pico.draw_*/fill_*/clear_rect): ウィジェットを介さず、全ウィジェットが
// 描いている共有フレーム(OSData::frame)へ直接描く。座標はpico.content_rect()と同じ
// 絶対スクリーン座標、色は既存プロパティ(border_color等)と同じPICO 4bitパレット番号(0〜15)。
//
// 【重要】loop()/コールバックから素で呼んでも表示は持続しない。PICO_GFX::FlushDirty()は
// dirty矩形ごとに「それを覆うウィジェットが無ければ背景色で塗りつぶしてから、
// そこに重なるウィジェットだけを再描画する」ため、ウィジェットに属さない場所への
// 直接描画は次にその領域がdirtyになった瞬間(シーン遷移時の全画面dirty化を含め、
// ほぼ必ず起きる)に消え、誰も描き直さないので二度と戻らない(PCビルドの--shotで
// fill_rectが跡形もなく消えることを確認済み)。
//
// 正しく持続させるには LuaCanvas(pico.create("Canvas")) に乗せ、
// pico.on(canvas_id, "render", fn) で登録したコールバックの中からpico.draw_*を
// 呼ぶこと。render()はFlushDirty()の合成サイクルの中で呼ばれるので、そのたび
// 全部を描き直せば正しく生き残る(CanvasRasterが自前スプライトで同じ問題を
// 解決しているのと同じ理屈)。再描画のリクエストは:
//   - pico.invalidate(id)      … そのウィジェットの矩形をdirty化(次のFlushDirty()で
//                                 render()経由のコールバックが呼ばれる)
//   - pico.mark_dirty(x,y,w,h) … PICO_GFX::MarkDirty()の生の下請け。Canvasに限らず
//                                 任意の矩形を直接dirty化したいとき向けの低レベルAPI
// 静的な内容は生成直後の自動描画(新規ウィジェットは初期状態でdirty)だけで映るので、
// 毎フレーム描き直す必要が無い。アニメーションはloop()から都度invalidate()すればよい。
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

        // Arduino風のsetup()/loop()呼び出し。グローバル関数として定義されていなければ
        // 何もしない(必須ではない)。
        //
        // CallSetup(): Run()成功後に1回だけ呼ぶ想定。setup()自体がエラーだった場合は
        // Run()と同じくErrorFunctions::ShowFatal()で表示するだけで、以降loop()を
        // 呼び続けるかどうかは呼び出し側(LuaScene)の判断に委ねる。
        //
        // CallLoop(): 毎フレーム呼ぶ想定。setup()と違い「毎フレーム同じエラーが
        // 出続ける」ことがあり得るため、一度エラーになったら内部で以降のloop()
        // 呼び出しを自動的に止める(でなければMsgDialogが毎フレーム積まれて画面が壊れる)。
        // dt_msは前回の呼び出しからの経過ミリ秒で、loop(dt)としてLua側へ渡す。
        void CallSetup();
        void CallLoop(uint32_t dt_ms);

    private:
        // Render: LuaCanvas限定。他4種はWidget基底が全種別共通で持つ(BindCallback参照)
        enum class EventKind : uint8_t { PressStart, PressEnd, PressMove, PressOut, Render };

        struct CallbackBinding {
            WidgetId id;
            EventKind kind;
            int ref; // LUA_REGISTRYINDEXに積んだLua関数への参照
        };

        lua_State* L = nullptr;
        size_t budget_;
        size_t used_ = 0;

        // loop()が一度エラーを出したら以降は呼ばない(毎フレーム同じエラーダイアログが
        // 積まれるのを防ぐ安全弁)。setup()側はRun()と同じく1回きりなので不要
        bool loop_broken_ = false;

        std::vector<CallbackBinding> callbacks_;

        // グローバル関数nameを引数無しで呼ぶ(setup()向け)。定義されていなければ何もしない。
        // エラー時はErrorFunctions::ShowFatal()で表示する
        void callGlobalNoArgs(const char* name);

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
        static int l_invalidate(lua_State* L);
        static int l_mark_dirty(lua_State* L);

        // 直接描画。クラスコメント「直接描画」参照。いずれも描画後に自分の描いた
        // 範囲をPICO_GFX::MarkDirty()する(LuaCanvasのrenderコールバック内で呼ぶ
        // 場合はFlushDirty()側がisDirtyDeactivates=trueにしている最中なので無害な
        // no-opになる。loop()等から素で呼んだ場合は表示が持続しない点に注意)
        static int l_draw_pixel(lua_State* L);
        static int l_draw_line(lua_State* L);
        static int l_draw_rect(lua_State* L);
        static int l_fill_rect(lua_State* L);
        static int l_draw_circle(lua_State* L);
        static int l_fill_circle(lua_State* L);
        static int l_clear_rect(lua_State* L);
        static int l_draw_text(lua_State* L);

        // 直接描画エリア(クリップ矩形)。OSData::frameへのpico.draw_*/draw_text呼び出しを
        // この矩形の内側だけに制限する。set_draw_areaを呼びっぱなしでrenderコールバックを
        // 抜けると、以降そのCanvas以外の描画(他ウィジェットのrender()を含む)まで
        // 同じ矩形に切り詰められてしまう(OSData::frameは全ウィジェット共有のスプライトで、
        // クリップ矩形もその1個しか無いため)。この事故を防ぐため、LuaCanvas::render()が
        // renderコールバックから戻った直後に必ずclearClipRect()する安全弁を入れてある
        // (LuaCanvas.cpp参照)。スクリプト側がclear_draw_area()を呼び忘れても、
        // 少なくとも「そのCanvas以外を巻き込む」事故には至らない。
        static int l_set_draw_area(lua_State* L);
        static int l_clear_draw_area(lua_State* L);

        // SDカードアクセス。パスはSD_Functions/FileExplorerと同じくSD絶対パス。
        // OSData::SD_usable==falseの間はどれも「失敗」(false/nil)を返すだけで、
        // luaL_errorにはしない(SD無しはプログラマの誤りではなく実行時の状態のため)。
        // sd_read/sd_writeはFsFileを開いたままLuaのAPI(luaL_Buffer/テーブル構築等)を
        // 呼ぶため、その最中にLua側がメモリ予算超過でエラー(longjmp)するとFsFileの
        // 後始末(close())が飛ばされ得る。ANSI Cのsetjmp/longjmpベースなので
        // C++デストラクタも呼ばれない。ごく小さな読み書きの最中に限られる稀な
        // エッジケースであり、OS内部の90箇所のOOM未対応(CLAUDE.md参照)と同じ
        // 割り切りで対象外とする。
        static int l_sd_exists(lua_State* L);
        static int l_sd_read(lua_State* L);
        static int l_sd_write(lua_State* L);
        static int l_sd_remove(lua_State* L);
        static int l_sd_mkdir(lua_State* L);
        static int l_sd_list(lua_State* L);

        // pico.sd_read()が1回で読む上限。LuaScene::kMaxScriptBytesと同じ考え方
        // (Lua state全体の予算(通常200KB)を1ファイルで食い潰さないための頭打ち)。
        // スクリプト読み込みと違い「打ち切って使う」のは壊れたデータを黙って
        // 渡すことになるため、超過時は切り詰めずnilを返す(呼び出し側で判別可能)。
        static constexpr size_t kMaxSdReadBytes = PICO_STR_16KiB;
};
