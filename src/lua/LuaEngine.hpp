#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "lua.hpp"
#include "gui/widgets/WidgetID.hpp"
#include "gui/icons/icon_render.h"
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
//
// 画像(pico.image_load/draw_image/image_size/image_free): ウィジェット(Image)を
// 介さず、`.pimg`(IconRender参照)を直接デコードしてこのLuaEngineインスタンスが
// 保持する。ウィジェットではないのでWidgetRegistryは使わず、固定長スロット配列
// (images_、上限kMaxLuaImages枚)を自分で持つ。ハンドルはWidgetIdと同じ発想の
// 「generation(上位)+index(下位)」パック整数だが、この配列専用のスコープなので
// 32bit全体を使う本家の配分(type/generation/index)は真似ず単純化してある。
// generationはWidgetRegistryと同じく解放(pico.image_free)のたびに1つ進める
// (使用中かどうかは別途ImageSlot::usedで見る。generationを未使用の合図に
// 使い回すと、解放直後に同じスロットを再利用したとき前回と同じハンドル値を
// 発行してしまい、古い(解放済みの)ハンドルが新しい画像を指す事故になるため)。
// generation==0は「一度もimage_loadに使われていないスロット」を表す予約値で、
// handle==0は常に無効。LGFX_Sprite(PimgSprite::sprite)はimages_の値メンバなので、
// pico.image_free()を呼び忘れてスクリプトが終了しても、LuaEngine自体の破棄
// (images_配列の破棄)でデストラクタが確保分を回収する(リークしない)。
// pico.image_free()はそれを待たず即座に解放したい場合向けの明示API。
//
// デコード後のピクセルバッファ(LGFX_Sprite::createSprite())はこのLuaEngineの
// budget_bytes(Alloc経由のLua自体の確保量)には乗らない、素のOSヒープ確保
// (WidgetFactory::Create()が作るウィジェット本体と同じ扱い)。Luaのメモリ予算とは
// 別に「同時に保持できる枚数」と「合計バイト数」の両方に頭打ちを設けてあるのは
// これが理由(kMaxLuaImages/kMaxLuaImageBytes参照)。
//
// シーン制御(pico.push_scene/change_scene/launch_app): pico.pop()(SceneFunctions::Pop())
// しか無かったため、Luaスクリプトは「自分を起動した画面へ戻る」以外の画面遷移が
// できなかった。C++側のSceneFunctions::Change/Push/Popに相当する3つを揃えた:
//   - push_scene(path) / change_scene(path) … 別のLuaスクリプトへ`SceneFunctions::Push/Change`
//     する。`new LuaScene(path)`を渡すだけで、Lua側が構築できる唯一のScene型が
//     LuaScene(パス文字列1つのコンストラクタ)であるため、この2つはLua同士の
//     画面遷移(複数画面のLuaアプリを作る)専用になる
//   - launch_app(name) … `AppFunctions::LaunchByName()`経由でランチャの登録簿を
//     名前引きし、C++製アプリも含め任意の既存アプリへ`Push`する。ランチャのタイルを
//     タップするのと同じ経路なので、Luaアプリから他のアプリへジャンプできる
// いずれも他のpico.pop()同様、要求を登録するだけでフレーム境界まで実行が保留される
// (SceneFunctions::Update()参照)。呼び出し中に今のLuaEngine自身が破棄されることはない。
// push_scene/change_sceneはLuaScene構築(パス文字列のコピーのみ)が失敗し得ないため
// 戻り値なし(pico.pop()と同じ)。launch_appだけは「名前が見つかったか」を
// その場で判定できるので、bool を返す(実際のPush自体の成否までは見ていない点は
// AppFunctions::Launch()を直接呼ぶC++コードと同じ)。
//
// ダイアログ(pico.show_message/show_input/show_file_save/show_file_select/show_color):
// `MsgDialog`/`InputDialog`/`FileSaveDialog`/`FileSelectDialog`/`ColorDialog`は
// `WidgetFactory::Create()`の対象外(コンストラクタが型ごとに必須の引数を取り、
// 15種の汎用部品のような「位置0,0・空文字列」といった無難な既定値で作れないため)。
// 代わりに各ダイアログ専用の生成関数を用意し、内部で`new Xxx(...)`→
// `WidgetFunctions::AddDialog()`→`setVisible(true)`まで済ませて`WidgetId`を返す
// (`WidgetId`の発行自体はどのWidgetサブクラスでも`getId()`初回呼び出しで汎用に効くので、
// `WidgetFactory`を経由しなくても問題ない)。
// 閉じたときの通知は共通の`pico.on(id, "closed", function(id, is_ok) ... end)`で受ける
// (`EventKind::Closed`。他の4イベントと同じ`callbacks_`の対応表に乗せるが、
// 実際のC++側コールバック配線は生成時点(show_xxx内)で済ませてしまう —
// ダイアログは`pico.on()`を呼び忘れても画面に residual として残り続けてはいけない
// モーダルなので、「閉じたら`WidgetFunctions::DestroyLater()`する」までを生成時に
// 保証し、`pico.on()`は「あれば追加でLuaへも通知する」という上乗せの位置づけにしてある)。
// `InputDialog`の入力文字列/`FileSaveDialog`・`FileSelectDialog`の選択パス/
// `ColorDialog`の選択色は、専用の戻り値をコールバックへ積むのではなく、
// 既存の`pico.get(id, "text"/"path"/"value")`(`WidgetProperty`)経由で読む設計にした
// (`Dispatch()`をダイアログの具象型に依存させたくないため。`MsgDialog`は
// `is_ok`だけで完結するので追加のプロパティは無い)。
//
// ネットワーク(pico.http_request/http_cancel): 既存の`Http_Get`はMarkdownブラウザの
// キャッシュ用途(GET専用、200/304以外は本文を捨てて一律失敗扱い)に特化しているため、
// 汎用のHTTPクライアントとしては使えない。新設した`HttpRequest`
// (`src/task/Http_Request.hpp`。メソッド・送信ボディを指定でき、ステータスコードに
// よらず本文を渡す)をこのLuaEngineインスタンスが1本だけ(`http_`、遅延`new`)保持し、
// `LuaScene::onUpdate()`から毎フレーム`UpdateHttp()`で進める(`HttpGet`/`HttpRequest`は
// どちらも`PICO_Task`の全体リストには登録されず、所有側が自分で`update()`を呼ぶ設計の
// ため)。**同時に実行できるリクエストは1本まで**で、完了(成功/失敗)すると
// `pico.http_request()`に渡したLua関数を`(ok, status_code, body_or_nil, error_or_nil)`
// で呼んでから後片付けする。`http_`はLuaScene内でネットワークを使わないアプリに
// 16KiB×2の固定バッファを常時負担させたくないため、初回の`pico.http_request()`呼び出し
// まで確保しない(遅延生成。使わないLuaアプリのメモリコストはゼロのまま)。
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

        // 進行中のpico.http_request()を1フレーム分進める。LuaScene::onUpdate()から
        // 毎フレーム呼ぶ想定(クラスコメント「ネットワーク」参照)。リクエストが
        // 無ければ何もしない
        void UpdateHttp();

    private:
        // Render: LuaCanvas限定。Closed: ダイアログ限定。他4種はWidget基底が
        // 全種別共通で持つ(BindCallback参照)
        enum class EventKind : uint8_t { PressStart, PressEnd, PressMove, PressOut, Render, Closed };

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

        // pico.image_* が使う画像スロット。ウィジェットの生成数のように実行時に
        // 増減する必要が無い(1つのLuaアプリが同時に扱う画像は少数の見込み)ため、
        // MarkdownViewのプールと同じ「固定長配列」志向で、std::vector等は使わない。
        struct ImageSlot {
            IconRender::PimgSprite sprite;
            bool used = false;
            // WidgetRegistryと同じ「解放のたびに進める」方式。usedがfalseの間も
            // 値は保持したままにする(次にこのスロットを使い回したときのハンドルを
            // 前回発行分と別物にするため。generation==0は「一度も使われていないスロット」
            // を表す予約値で、初回使用時に1へ進める)
            uint32_t generation = 0;
            size_t bytes = 0; // 使用中のバイト数(image_bytes_used_の増減用に覚えておく)
        };

        // 同時に保持できる画像の枚数と合計バイト数の上限。前者はスロット数の頭打ち、
        // 後者は「小さい画像を大量に」でも予算を使い切れるようにするための頭打ち
        // (LGFX_Sprite側の確保はLuaEngineのbudget_/Alloc経由の予算に乗らないため、
        // ここで別枠として管理する。上のクラスコメント参照)。
        static constexpr size_t kMaxLuaImages = 4;
        static constexpr size_t kMaxLuaImageBytes = 64 * 1024;

        ImageSlot images_[kMaxLuaImages];
        size_t image_bytes_used_ = 0;

        // generation(上位24bit)+index(下位8bit、1始まり。0は無効値)のパック整数。
        // WidgetIdと違いこの配列専用のスコープなので32bit全体を型ビットまで使う必要はない
        static uint32_t MakeImageHandle(size_t index, uint32_t generation);
        bool ResolveImageHandle(uint32_t handle, size_t& out_index) const;

        // pico.http_request()用の状態(進行中のHttpRequest+受信バッファ+完了コールバック)。
        // 使わないLuaアプリのメモリコストをゼロに保つため、初回のpico.http_request()まで
        // newしない(完全な定義は.cppのみ。クラスコメント「ネットワーク」参照)
        struct HttpState;
        HttpState* http_ = nullptr;

        // ダイアログが閉じたときのC++側コールバック配線(pico.show_xxx()内で生成直後に
        // 必ず呼ぶ。pico.on()の有無に関わらずDestroyLater()までを保証する。
        // クラスコメント「ダイアログ」参照)
        void WireDialogClosed(class Widget* dialog, WidgetId id);
        // EventKind::Closed専用のDispatch。is_okを2つ目の引数としてLua関数へ渡す点だけ
        // 通常のDispatch(id, kind)と異なる(そちらはWidgetIdの1引数固定のまま変えていない)
        void DispatchClosed(WidgetId id, bool is_ok);

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
        // シーン制御。クラスコメント「シーン制御」参照。push_scene/change_sceneは
        // 別のLuaスクリプトへ、launch_appは登録簿の任意のアプリ(C++製含む)へ飛ぶ
        static int l_push_scene(lua_State* L);
        static int l_change_scene(lua_State* L);
        static int l_launch_app(lua_State* L);
        static int l_content_rect(lua_State* L);

        // ダイアログ。クラスコメント「ダイアログ」参照。いずれも生成した
        // WidgetId(整数)を返す。閉じたときの結果はpico.on(id,"closed",fn)
        // (is_okのbool)と、必要ならpico.get(id, "text"/"path"/"value")で受け取る
        static int l_show_message(lua_State* L);
        static int l_show_input(lua_State* L);
        static int l_show_file_save(lua_State* L);
        static int l_show_file_select(lua_State* L);
        static int l_show_color(lua_State* L);

        // ネットワーク。クラスコメント「ネットワーク」参照
        static int l_http_request(lua_State* L);
        static int l_http_cancel(lua_State* L);
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
        // pico.draw_image()も他のpico.draw_*と同じく直接描画の一種(LuaCanvasの
        // renderコールバックの中で使うこと)なのでここに置くが、ハンドルの発行・
        // 解放自体はrenderコールバックの外(setup()等)で自由に呼んでよい
        static int l_draw_image(lua_State* L);

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

        // 画像(pico.image_load/image_size/image_free)。クラスコメント「画像」参照。
        // `.pimg`をこのLuaEngineインスタンスの固定長スロットへデコードして持ち、
        // 整数ハンドルで扱う(WidgetIdと違いWidgetRegistryは経由しない)。
        // 読み込み失敗(パス不正・SD無し・スロット/バイト予算超過・不正な.pimg)は
        // pico.sd_read等と同じくnilを返すだけでluaL_errorにはしない。
        // 一方、無効なハンドルをdraw_image/image_sizeへ渡すのはプログラマの誤りとして
        // pico.set/pico.get同様luaL_errorにする(image_freeだけはpico.destroyと同じく
        // 二重解放を黙って許容する)。
        static int l_image_load(lua_State* L);
        static int l_image_size(lua_State* L);
        static int l_image_free(lua_State* L);

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
