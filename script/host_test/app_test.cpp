// アプリ登録簿(AppFunctions)とランチャのタイル配置(AppGrid)を検証するテスト。
//
// AppGridはタイルごとにウィジェットを持たず、render()で直接描いてタップ位置から
// 対象を逆算する。つまり「描く位置」と「当たり判定の位置」が別々の計算になっていて、
// ずれても画面を見るまで気付けない。ここで両者の整合を突き合わせる。
#include "services/App_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "apps/home/AppGrid.hpp"
#include "gui/core/Scene.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <cstring>
#include <string>

// ---- モック ----
void PICO_GFX::MarkDirty(const Rect&){}
void PICO_GFX::Setup(){}
void PICO_GFX::FlushDirty(){}
void PICO_GFX::DrawDialogBackground(){}
void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

// Launch()は本物を動かしたいので、その先のSceneFunctions側を差し替える。
// Push()はシーンの所有権を引き取るので、ここで解放しないとASanに拾われる
static Scene* pushed_scene = nullptr;
void SceneFunctions::Push(Scene* next){
    delete pushed_scene;
    pushed_scene = next;
}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
// 落ちずに通ったこと自体を結果とするケース(ASanが範囲外アクセスを検出する)
static void ok(const char* label){
    printf("[ OK ] %s\n", label);
}

static void eq(int actual, int expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %-46s 実測=%d 期待=%d\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if(!ok) failures++;
}

// 登録用のダミーシーン
class DummyScene : public Scene {
    public:
        const char* getName() const override { return "Dummy"; }
        void onEnter() override {}
};

// 引数(AppEntry::arg)を受け取るダミーシーン。
// 生成時に渡された文字列をここへ控えて、登録簿が引数を運べているかを確かめる
static FixedString<PICO_STR_L> last_arg;

class ArgScene : public Scene {
    public:
        explicit ArgScene(const char* path){ (void)last_arg.assign(path); }
        const char* getName() const override { return "Arg"; }
        void onEnter() override {}
};

static void registerApps(int n){
    AppFunctions::Clear();
    //名前はRegister()がコピーして持つので、スタック上の一時バッファから登録してよい
    //(以前は静的寿命のリテラルが必須だった。ASanの下で寿命切れ参照も検出できる)
    for(int i = 0; i < n; i++){
        char name[16];
        snprintf(name, sizeof(name), "a%02d", i + 1);
        AppFunctions::Register(name, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
    }
}

int main(){
    // ---- 登録簿 ----
    {
        AppFunctions::Clear();
        eq(AppFunctions::Count(), 0, "登録簿: 初期状態は0件");
        check(AppFunctions::Get(0) == nullptr, "登録簿: 範囲外はnullptr");

        check(AppFunctions::Register("テスト", IconID::AppBox,
                                     &AppFunctions::MakeScene<DummyScene>),
              "登録簿: 登録できる");
        eq(AppFunctions::Count(), 1, "登録簿: 件数が増える");

        const AppEntry* e = AppFunctions::Get(0);
        check(e != nullptr && e->create != nullptr, "登録簿: 生成関数が引ける");
        if(e && e->create){
            Scene* s = e->create(*e);
            check(s != nullptr, "登録簿: 生成関数がシーンを返す");
            delete s;
        }
        check(e != nullptr && strcmp(e->name.c_str(), "テスト") == 0,
              "登録簿: 名前がコピーされている");

        check(!AppFunctions::Register(nullptr, IconID::AppBox,
                                      &AppFunctions::MakeScene<DummyScene>),
              "登録簿: 名前なしは拒否する");
        check(!AppFunctions::Register("x", IconID::AppBox, nullptr),
              "登録簿: 生成関数なしは拒否する");
        eq(AppFunctions::Count(), 1, "登録簿: 拒否された分は増えない");
    }

    // ---- 引数つきの登録(同じシーン型を別の中身で並べる) ----
    // Luaアプリが「同じLuaScene型 + 別スクリプトパス」で増える形の土台になる部分
    {
        AppFunctions::Clear();

        //パスも寿命の短いバッファから渡せること
        char path[32];
        snprintf(path, sizeof(path), "tmp/doc.md");
        check(AppFunctions::Register("文書A", IconID::File,
                                     &AppFunctions::MakeSceneWithArg<ArgScene>, path),
              "引数つき: 登録できる");

        snprintf(path, sizeof(path), "tmp/other.md");
        check(AppFunctions::Register("文書B", IconID::File,
                                     &AppFunctions::MakeSceneWithArg<ArgScene>, path),
              "引数つき: 同じ生成関数を別のargで登録できる");
        eq(AppFunctions::Count(), 2, "引数つき: 2件とも登録される");

        const AppEntry* a = AppFunctions::Get(0);
        const AppEntry* b = AppFunctions::Get(1);
        check(a && strcmp(a->arg.c_str(), "tmp/doc.md") == 0, "引数つき: argがコピーされている");
        check(b && strcmp(b->arg.c_str(), "tmp/other.md") == 0, "引数つき: 2件目のargも独立している");
        check(a && b && a->create == b->create, "引数つき: 生成関数は共有されている");

        //生成関数までargが届くこと
        last_arg.clear();
        Scene* s = a->create(*a);
        check(strcmp(last_arg.c_str(), "tmp/doc.md") == 0, "引数つき: 生成関数へargが渡る");
        delete s;

        //argを使わないアプリは空のまま
        AppFunctions::Register("引数なし", IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        const AppEntry* c = AppFunctions::Get(2);
        check(c && c->arg.empty(), "引数つき: argを省いた登録では空になる");
    }

    // ---- 長すぎる名前/引数 ----
    {
        AppFunctions::Clear();

        //名前は表示が縮むだけなので、切り詰めた上で登録は通す
        char long_name[PICO_STR_M * 2];
        memset(long_name, 'n', sizeof(long_name) - 1);
        long_name[sizeof(long_name) - 1] = '\0';
        check(AppFunctions::Register(long_name, IconID::AppBox,
                                     &AppFunctions::MakeScene<DummyScene>),
              "長さ: 長すぎる名前でも登録は通る");
        const AppEntry* e = AppFunctions::Get(0);
        check(e && e->name.length() < PICO_STR_M, "長さ: 名前は切り詰められている");

        //argはパスなので、切り詰まると別物を指す。登録ごと拒否する
        char long_arg[PICO_STR_L * 2];
        memset(long_arg, 'p', sizeof(long_arg) - 1);
        long_arg[sizeof(long_arg) - 1] = '\0';
        check(!AppFunctions::Register("長い引数", IconID::AppBox,
                                      &AppFunctions::MakeSceneWithArg<ArgScene>, long_arg),
              "長さ: 長すぎる引数は登録を拒否する");
        eq(AppFunctions::Count(), 1, "長さ: 拒否された分は増えない");
    }

    // ---- 上限 ----
    {
        registerApps(AppFunctions::kMaxApps);
        eq(AppFunctions::Count(), AppFunctions::kMaxApps, "登録簿: 上限まで登録できる");
        check(!AppFunctions::Register("溢れ", IconID::AppBox,
                                      &AppFunctions::MakeScene<DummyScene>),
              "登録簿: 上限を超えた登録は拒否する");
        eq(AppFunctions::Count(), AppFunctions::kMaxApps, "登録簿: 上限超過で件数は変わらない");
    }

    // ---- ページ数 ----
    // 実機と同じ領域(240x300: 画面からステータスバーを除いた大きさ)で確かめる
    {
        registerApps(0);
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        const int per_page = g.tilesPerPage();
        check(per_page >= 4, "1ページに4個以上並ぶ");
        eq(g.pageCount(), 1, "アプリ0件でもページ数は1");

        registerApps(1);
        eq(g.pageCount(), 1, "1件なら1ページ");

        registerApps(per_page);
        eq(g.pageCount(), 1, "ちょうど1ページぶんなら1ページ");

        registerApps(per_page + 1);
        eq(g.pageCount(), 2, "1つ溢れると2ページ");
    }

    // ---- タイルの当たり判定 ----
    {
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        const int per_page = g.tilesPerPage();
        registerApps(per_page);

        //render()が使うタイル矩形の中心を叩いて、そのタイルのindexが返ること。
        //「描く位置」と「当たり判定の位置」が同じ矩形から来ていることの確認になる
        bool all_hit = true;
        bool no_overlap = true;
        for(int slot = 0; slot < per_page; slot++){
            const Rect t = g.tileRect(slot);
            if(g.hitTile(t.x + t.w / 2, t.y + t.h / 2) != slot) all_hit = false;

            //4隅の内側も同じタイルに入ること(矩形どうしが重なっていないことの裏取り)
            if(g.hitTile(t.x, t.y) != slot) no_overlap = false;
            if(g.hitTile(t.x + t.w - 1, t.y + t.h - 1) != slot) no_overlap = false;
        }
        check(all_hit, "各タイルの中心を叩くとそのindexが返る");
        check(no_overlap, "各タイルの隅も自分のindexになる(矩形が重なっていない)");

        eq(g.hitTile(-5, 10), -1, "左外は-1");
        eq(g.hitTile(10, -5), -1, "上外は-1");
        eq(g.hitTile(SCREEN_WIDTH + 10, 10), -1, "右外は-1");
        eq(g.hitTile(10, SCREEN_HEIGHT * 2), -1, "下外は-1");
        eq(g.hitTile(0, 0), -1, "外周の余白は-1(タイルの外)");

        //名前がはみ出さないよう2列へ広げた経緯があるので、幅を下限として固定しておく。
        //Smallフォントの日本語は1文字16pxなので、96pxあれば1行6文字入る
        const Rect t = g.tileRect(0);
        check(t.w >= 96, "タイル幅は日本語6文字ぶん(96px)以上ある");
    }

    // ---- 空きスロットは起動しない ----
    {
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        registerApps(1); //1ページに空きが大量に出る状態
        const Rect t0 = g.tileRect(0);
        const Rect t1 = g.tileRect(1);
        eq(g.hitTile(t0.x + t0.w / 2, t0.y + t0.h / 2), 0, "先頭タイルは引ける");
        //2つ目のタイルの位置には何も登録されていない
        eq(g.hitTile(t1.x + t1.w / 2, t1.y + t1.h / 2), -1, "空きスロットは-1");
    }

    // ---- ページ送り ----
    {
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        const int per_page = g.tilesPerPage();
        registerApps(per_page + 2);

        eq(g.getPage(), 0, "初期ページは0");
        check(!g.prevPage(), "先頭では前ページへ行けない");
        check(g.nextPage(), "次ページへ行ける");
        eq(g.getPage(), 1, "ページが進む");
        check(!g.nextPage(), "最終ページでは次へ行けない");

        //2ページ目の先頭タイルは per_page 番目のアプリ
        const Rect t0 = g.tileRect(0);
        const int cx = t0.x + t0.w / 2;
        const int cy = t0.y + t0.h / 2;
        eq(g.hitTile(cx, cy), per_page, "2ページ目の先頭タイルのindex");

        check(g.prevPage(), "前ページへ戻れる");
        eq(g.hitTile(cx, cy), 0, "戻ると先頭アプリに戻る");
    }

    // ---- 高さを変えるとページの数え直しが要る ----
    // HomeSceneはページ送りを出すときにグリッドの高さを詰めるので、その経路
    {
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        const int full_tiles = g.tilesPerPage();
        registerApps(full_tiles);
        eq(g.pageCount(), 1, "詰める前は1ページ");

        //1行ぶん(タイル高+間隔)を削ると、その行が次ページへ送られる
        g.setH(SCREEN_HEIGHT - STATUSBAR_HEIGHT - 64);
        check(g.tilesPerPage() < full_tiles, "高さを詰めると1ページのタイル数が減る");
        check(g.pageCount() >= 2, "その結果ページが増える");
    }
    {
        //最終ページにいる状態で高さを広げ、ページ数が減った場合に範囲外へ取り残されないこと
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT - 64);
        registerApps(g.tilesPerPage() + 1);
        while(g.nextPage()) {}
        check(g.getPage() > 0, "最終ページへ移動できる");

        g.setH(SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        check(g.getPage() < g.pageCount(), "高さを広げてもページが範囲外に残らない");
    }

    // ---- タップで起動される ----
    {
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        registerApps(3);
        int got = -1;
        g.setOnLaunch([&got](int index){ got = index; });

        //2つ目のタイルの中心を押して離す(スクリーン座標なのでグリッドのY起点を足す)
        const Rect t1 = g.tileRect(1);
        OSData::touchX = t1.x + t1.w / 2;
        OSData::touchY = STATUSBAR_HEIGHT + t1.y + t1.h / 2;
        g.causeOnPressStart();
        g.causeOnPressEnd();
        eq(got, 1, "押して離したタイルのアプリが起動される");

        //タイルの無いところを押しても起動しない
        got = -1;
        OSData::touchX = 0;
        OSData::touchY = STATUSBAR_HEIGHT;
        g.causeOnPressStart();
        g.causeOnPressEnd();
        eq(got, -1, "タイルの外を押しても起動しない");
    }

    // ---- 名前の描画 ----
    // 名前は最大2行へ自前で折り返している。UTF-8の文字境界で切る処理と
    // 固定長バッファへのコピーがあるので、ASanの下で実際に描いて確かめる。
    // 幅が極端に狭い場合に1文字も進まず無限ループしないことも兼ねる
    {
        AppFunctions::Clear();
        static const char* kHardNames[] = {
            "",                              // 空
            "a",                             // 1文字
            "入力テスト",                     // 3列だとはみ出していた長さ
            "ファイルエクスプローラー",       // 2行でちょうど収まるくらい
            "とても長い名前のアプリケーション名テスト", // 2行でも収まらない
            "Mixed 日本語 and ASCII 混在",    // 混在
        };
        for (const char* n : kHardNames) {
            AppFunctions::Register(n, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        }

        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        g.render();
        ok("名前の描画: 長短/混在の名前で範囲外アクセスなし");

        //タイルが極端に狭い場合(1文字も入らない幅)でも進む
        AppGrid narrow(0, STATUSBAR_HEIGHT, 40, 120);
        narrow.render();
        ok("名前の描画: 極端に狭いタイルでも止まらない");
    }

    // ---- Launch()が登録簿のシーンを作ってPushする ----
    {
        registerApps(2);
        AppFunctions::Launch(1);
        check(pushed_scene != nullptr, "Launch: シーンが生成されてPushされる");

        //Push()側のモックは渡された物をそのまま持つので、nullへ戻してから叩けば
        //「何もPushされなかった」ことを実際に確かめられる
        delete pushed_scene;
        pushed_scene = nullptr;
        AppFunctions::Launch(99);
        check(pushed_scene == nullptr, "Launch: 範囲外indexでは何もPushしない");

        //argつきのアプリを起動すると、生成関数を経由してargがシーンへ届く
        AppFunctions::Clear();
        AppFunctions::Register("文書", IconID::File,
                               &AppFunctions::MakeSceneWithArg<ArgScene>, "tmp/launched.md");
        last_arg.clear();
        AppFunctions::Launch(0);
        check(pushed_scene != nullptr, "Launch: argつきのアプリも起動できる");
        check(strcmp(last_arg.c_str(), "tmp/launched.md") == 0,
              "Launch: 起動時にargがシーンへ渡る");
    }
    delete pushed_scene;
    pushed_scene = nullptr;

    //---- タイルアイコン: argのディレクトリからの相対パスだけを持ち、絶対パスはIconPathOf()で組み立てる ----
    {
        AppFunctions::Clear();
        FixedString<PICO_PATH_LEN> path;
        check(AppFunctions::Register("A", IconID::AppBox, &AppFunctions::MakeSceneWithArg<ArgScene>,
                                     "/lua/apps/テトリス/main.lua", LuaPermissions{}, "/lua/apps/テトリス/icon.pimg"),
              "icon: 登録できる");
        const AppEntry* a = AppFunctions::Get(0);
        check(a->icon_file == "icon.pimg", "icon: 相対パスだけを持つ");
        check(AppFunctions::IconPathOf(*a, path) && path == "/lua/apps/テトリス/icon.pimg", "icon: 絶対パスに戻せる");

        check(AppFunctions::Register("B", IconID::AppBox, &AppFunctions::MakeSceneWithArg<ArgScene>,
                                     "/lua/apps/b/main.lua", LuaPermissions{}, "/lua/apps/b/img/i.pimg"),
              "icon: サブディレクトリも登録できる");
        check(AppFunctions::IconPathOf(*AppFunctions::Get(1), path) && path == "/lua/apps/b/img/i.pimg",
              "icon: サブディレクトリの絶対パス");

        //アプリのディレクトリの外・長すぎる相対パスは既定アイコンへ(登録自体は通す)
        check(AppFunctions::Register("C", IconID::AppBox, &AppFunctions::MakeSceneWithArg<ArgScene>,
                                     "/lua/apps/c/main.lua", LuaPermissions{}, "/img/other.pimg"),
              "icon: 外のアイコンでも登録は通る");
        check(AppFunctions::Get(2)->icon_file.empty() && !AppFunctions::IconPathOf(*AppFunctions::Get(2), path),
              "icon: 外のアイコンは持たない");
        check(AppFunctions::Register("D", IconID::AppBox, &AppFunctions::MakeSceneWithArg<ArgScene>,
                                     "/lua/apps/d/main.lua", LuaPermissions{},
                                     "/lua/apps/d/very/long/directory/name/icon.pimg"),
              "icon: 長いアイコンでも登録は通る");
        check(AppFunctions::Get(3)->icon_file.empty(), "icon: 収まらない相対パスは持たない");
        //前方だけ一致する別ディレクトリ(/lua/apps/dd)は外扱い
        check(AppFunctions::Register("E", IconID::AppBox, &AppFunctions::MakeSceneWithArg<ArgScene>,
                                     "/lua/apps/d/main.lua", LuaPermissions{}, "/lua/apps/dd/icon.pimg"),
              "icon: 登録は通る");
        check(AppFunctions::Get(4)->icon_file.empty(), "icon: 名前の頭だけ同じディレクトリは外");
        //argが無いアプリはアイコンファイルを持てない
        check(AppFunctions::Register("F", IconID::AppBox, &AppFunctions::MakeScene<DummyScene>,
                                     nullptr, LuaPermissions{}, "/x/icon.pimg"),
              "icon: argなしでも登録は通る");
        check(AppFunctions::Get(5)->icon_file.empty(), "icon: argなしはアイコンファイルを持たない");
    }

    // ---- ランチャの並び・非表示(/sys/launcher.cfg) ----
    {
        HostSd::files.clear();
        OSData::SD_usable = true;
        AppFunctions::Clear();
        const char* names[] = { "A", "B", "C", "D", "E" };
        for(const char* n : names) AppFunctions::Register(n, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        AppFunctions::LoadLayout(); //ファイルが無ければ登録順
        eq(AppFunctions::VisibleCount(), 5, "layout: 最初は全部見える");
        eq(AppFunctions::VisibleAt(2), 2, "layout: 最初は登録順");

        check(AppFunctions::MoveApp(3, -1), "layout: Dを前へ");
        eq(AppFunctions::VisibleAt(2), 3, "layout: Dが3番目");
        eq(AppFunctions::VisiblePosition(2), 3, "layout: Cは4番目");
        check(!AppFunctions::MoveApp(0, -1), "layout: 先頭より前へは動かない");
        check(AppFunctions::MoveAppToFront(4), "layout: Eを先頭へ");
        eq(AppFunctions::VisibleAt(0), 4, "layout: Eが先頭");
        check(HostSd::files.count("/sys/launcher.cfg") == 1, "layout: launcher.cfgへ書く");

        //隠す: 並びの位置は覚えたまま見えなくなる
        check(AppFunctions::SetHidden(0, true), "layout: Aを隠す");     //E A B D C → E B D C
        eq(AppFunctions::VisibleCount(), 4, "layout: 見える数が減る");
        eq(AppFunctions::HiddenCount(), 1, "layout: 隠した数");
        eq(AppFunctions::HiddenAt(0), 0, "layout: 隠したのはA");
        eq(AppFunctions::VisiblePosition(0), -1, "layout: 隠したものは位置なし");
        eq(AppFunctions::VisibleAt(1), 1, "layout: Bが2番目へ詰まる");
        //見えているものの間で動かす(隠したAは飛び越すが、相対位置は保つ)
        check(AppFunctions::MoveApp(1, -1), "layout: Bを前へ(隠したAを飛び越す)");
        eq(AppFunctions::VisibleAt(0), 1, "layout: Bが先頭");

        //書いたファイルから読み直すと同じになる(登録し直しても)
        const std::string saved = HostSd::files["/sys/launcher.cfg"];
        AppFunctions::Clear();
        for(const char* n : names) AppFunctions::Register(n, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        AppFunctions::Register("F", IconID::AppBox, &AppFunctions::MakeScene<DummyScene>); //新しく入れたアプリ
        AppFunctions::LoadLayout();
        eq(AppFunctions::VisibleAt(0), 1, "layout: 読み直し B");
        eq(AppFunctions::VisibleAt(1), 4, "layout: 読み直し E");
        eq(AppFunctions::VisibleAt(2), 3, "layout: 読み直し D");
        eq(AppFunctions::VisibleAt(3), 2, "layout: 読み直し C");
        eq(AppFunctions::VisibleAt(4), 5, "layout: 新しいアプリは最後");
        check(AppFunctions::IsHidden(0), "layout: 隠したまま");
        //消えたアプリ・知らない行は捨てる
        HostSd::files["/sys/launcher.cfg"] = "app = Z\nhidden = C\nfoo = A\napp = A\n";
        AppFunctions::LoadLayout();
        eq(AppFunctions::VisibleAt(0), 0, "layout: 知らない名前は飛ばす");
        check(AppFunctions::IsHidden(2), "layout: hidden = C");
        eq(AppFunctions::VisibleCount(), 5, "layout: Cだけ隠れる");

        //表示し直す・元に戻す
        check(AppFunctions::SetHidden(2, false), "layout: Cを表示");
        check(!AppFunctions::SetHidden(2, false), "layout: 変わらなければfalse");
        AppFunctions::ResetLayout();
        eq(AppFunctions::VisibleAt(0), 0, "layout: 元に戻すと登録順");
        check(HostSd::files.count("/sys/launcher.cfg") == 0, "layout: 元に戻すとファイルを消す");

        //ランチャ(AppGrid)は見えているものだけを並べる
        AppFunctions::SetHidden(0, true);
        AppGrid grid(0, 20, 240, 300);
        const Rect t0 = grid.tileRect(0);
        eq(grid.hitTile(t0.x + 5, t0.y + 5), 1, "layout: 先頭のタイルは隠したAではなくB");
        grid.showApp(5);
        eq(grid.focusedApp(), 5, "layout: showAppでそのタイルを選ぶ");
        eq(grid.getPage(), 0, "layout: 6個目(見えているのは5個)は1ページ目");
    }

    // ---- ランチャのフォルダ ----
    {
        HostSd::files.clear();
        OSData::SD_usable = true;
        AppFunctions::Clear();
        const char* names[] = { "A", "B", "C", "D", "E", "F" };
        for(const char* n : names) AppFunctions::Register(n, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        AppFunctions::LoadLayout();
        AppFunctions::TopItem t;
        eq(AppFunctions::TopCount(), 6, "folder: 最初はトップに6つ");
        eq(AppFunctions::FolderCount(), 0, "folder: 最初はフォルダ無し");

        //Bの位置にフォルダを作る: A [G:B] C D E F
        const int g = AppFunctions::CreateFolder("  ゲーム ", 1);
        check(g >= 0, "folder: 作れる");
        check(strcmp(AppFunctions::FolderName(g), "ゲーム") == 0, "folder: 名前の前後の空白は落とす");
        eq(AppFunctions::TopCount(), 6, "folder: 作ってもトップの数は同じ");
        check(AppFunctions::TopAt(1, t) && t.is_folder && t.index == g, "folder: Bの位置にフォルダ");
        eq(AppFunctions::TopPosition(false, 1), -1, "folder: Bはトップに無い");
        eq(AppFunctions::FolderOf(1), g, "folder: Bはフォルダの中");

        //Eを入れる(フォルダの最後へ): A [G:B E] C D F
        check(AppFunctions::MoveToFolder(4, g), "folder: Eを入れる");
        check(!AppFunctions::MoveToFolder(4, g), "folder: 同じフォルダへは変わらない");
        eq(AppFunctions::FolderAppCount(g), 2, "folder: 中は2つ");
        eq(AppFunctions::FolderAppAt(g, 1), 4, "folder: Eは2番目");
        eq(AppFunctions::TopCount(), 5, "folder: トップは5つ");
        check(AppFunctions::TopAt(3, t) && !t.is_folder && t.index == 3, "folder: トップの4番目はD");
        eq(AppFunctions::VisibleCount(), 6, "folder: VisibleCountはフォルダの中も数える");

        //フォルダの中で動かす(トップは変わらない)
        check(AppFunctions::MoveApp(4, -1), "folder: 中でEを前へ");
        eq(AppFunctions::FolderAppAt(g, 0), 4, "folder: Eが中の先頭");
        check(!AppFunctions::MoveApp(4, -1), "folder: 中の先頭より前へは動かない(外へは出ない)");
        check(AppFunctions::TopAt(1, t) && t.is_folder, "folder: フォルダの位置は同じ");

        //トップのアプリはフォルダをまとめて越える: C を前へ → A C [G] D F
        check(AppFunctions::MoveApp(2, -1), "folder: Cを前へ");
        check(AppFunctions::TopAt(1, t) && !t.is_folder && t.index == 2, "folder: Cが2番目");
        check(AppFunctions::TopAt(2, t) && t.is_folder, "folder: フォルダが3番目");
        //フォルダを動かす: [G] A C D F
        check(AppFunctions::MoveFolderToFront(g), "folder: フォルダを先頭へ");
        check(AppFunctions::TopAt(0, t) && t.is_folder, "folder: フォルダが先頭");
        check(AppFunctions::MoveFolder(g, +2), "folder: フォルダを2つ後ろへ");
        eq(AppFunctions::TopPosition(true, g), 2, "folder: フォルダが3番目");    // A C [G] D F
        eq(AppFunctions::FolderAppAt(g, 1), 1, "folder: 動かしても中身はそのまま");

        //書いて読み直す(もう1つフォルダを作ってから)
        const int h = AppFunctions::CreateFolder("", 5);                         // A C [G:E B] D [H:F]
        check(h >= 0 && h != g, "folder: 2つ目");
        check(strcmp(AppFunctions::FolderName(h), "フォルダ") == 0, "folder: 空の名前は既定");
        check(AppFunctions::SetHidden(1, true), "folder: 中のBを隠す");
        eq(AppFunctions::FolderAppCount(g), 1, "folder: 隠すと中の数が減る");
        const std::string saved = HostSd::files["/sys/launcher.cfg"];
        check(saved.find("folder = ゲーム\nin = E\nin-hidden = B\n") != std::string::npos, "folder: launcher.cfgの形");
        AppFunctions::Clear();
        for(const char* n : names) AppFunctions::Register(n, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        AppFunctions::LoadLayout();
        eq(AppFunctions::FolderCount(), 2, "folder: 読み直し フォルダ2つ");
        eq(AppFunctions::TopCount(), 5, "folder: 読み直し トップ5つ");
        check(AppFunctions::TopAt(2, t) && t.is_folder && strcmp(AppFunctions::FolderName(t.index), "ゲーム") == 0,
              "folder: 読み直し ゲームが3番目");
        const int g2 = t.index;
        eq(AppFunctions::FolderAppAt(g2, 0), 4, "folder: 読み直し 中はE");
        check(AppFunctions::IsHidden(1) && AppFunctions::FolderOf(1) == g2, "folder: 読み直し 隠したBも中");

        //中を全部隠すとトップに出ない。表示し直すと戻る
        check(AppFunctions::SetHidden(4, true), "folder: Eも隠す");
        eq(AppFunctions::TopCount(), 4, "folder: 全部隠したフォルダはトップに出ない");
        eq(AppFunctions::FolderCount(), 2, "folder: フォルダ自体は残る");
        AppFunctions::SetHidden(4, false);
        AppFunctions::SetHidden(1, false);
        eq(AppFunctions::TopPosition(true, g2), 2, "folder: 表示し直すと元の位置");

        //出す: フォルダのすぐ後ろへ。最後の1つを出すとフォルダが消える
        check(AppFunctions::RemoveFromFolder(4), "folder: Eを出す");
        eq(AppFunctions::TopPosition(false, 4), 3, "folder: Eはフォルダのすぐ後ろ");
        check(AppFunctions::RemoveFromFolder(1), "folder: Bを出す");
        check(!AppFunctions::FolderExists(g2), "folder: 空になったフォルダは消える");
        eq(AppFunctions::FolderCount(), 1, "folder: 残りは1つ");

        //解除: 中のアプリはその場に出る
        int hf = -1;
        for(int f = 0; f < AppFunctions::kMaxFolders; f++) if(AppFunctions::FolderExists(f)) hf = f;
        const int hp = AppFunctions::TopPosition(true, hf);
        check(AppFunctions::RenameFolder(hf, "道具"), "folder: 名前を変える");
        check(!AppFunctions::RenameFolder(hf, "道具"), "folder: 同じ名前なら変わらない");
        check(AppFunctions::DissolveFolder(hf), "folder: 解除");
        eq(AppFunctions::FolderCount(), 0, "folder: 解除で無くなる");
        eq(AppFunctions::TopPosition(false, 5), hp, "folder: 中のFはその場に出る");

        //上限
        for(int i = 0; i < AppFunctions::kMaxFolders && i < 6; i++) AppFunctions::CreateFolder("x", i);
        eq(AppFunctions::FolderCount(), 6, "folder: 6つ作れる");
        eq(AppFunctions::TopCount(), 6, "folder: 1つずつ入れたのでトップの数は同じ");

        //登録簿から消えると中が詰まり、空のフォルダは消える
        AppFunctions::ResetLayout();
        const int k = AppFunctions::CreateFolder("k", 2);
        AppFunctions::MoveToFolder(3, k);                                          // A B [k:C D] E F
        check(AppFunctions::Unregister(2), "folder: Cを消す");
        eq(AppFunctions::FolderAppAt(k, 0), 2, "folder: 中のDのindexが詰まる");
        check(AppFunctions::Unregister(2), "folder: Dも消す");
        eq(AppFunctions::FolderCount(), 0, "folder: 空になったら消える");
        eq(AppFunctions::TopCount(), 4, "folder: トップは4つ");

        //手で書いたファイル: フォルダの中身が離れていても寄せる・中身の無いフォルダは捨てる
        AppFunctions::Clear();
        for(const char* n : names) AppFunctions::Register(n, IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        HostSd::files["/sys/launcher.cfg"] = "folder = 空\nfolder = X\nin = B\napp = A\nin = C\nfolder = Y\nin = Z\n";
        AppFunctions::LoadLayout();
        eq(AppFunctions::FolderCount(), 1, "folder: 中身の無いフォルダは捨てる");
        check(AppFunctions::TopAt(0, t) && t.is_folder && AppFunctions::FolderAppCount(t.index) == 1,
              "folder: app の行の後の in はフォルダの外");
        eq(AppFunctions::TopPosition(false, 2), 2, "folder: Cはトップ(Aの後ろ)");
        HostSd::files.erase("/sys/launcher.cfg");
        AppFunctions::ResetLayout();
    }

    // ---- アプリストアの入れ替え: FindByArg / Refresh / Unregister ----
    {
        AppFunctions::Clear();
        HostSd::files.erase("/sys/launcher.cfg");
        const char* names[] = {"A", "B", "C", "D", "E"};
        const char* args[] = {"/lua/apps/a/main.lua", "/lua/apps/b/main.lua", "/lua/apps/c/main.lua",
                              "/lua/apps/d/main.lua", "/lua/apps/e/main.lua"};
        for(int i = 0; i < 5; i++){
            AppFunctions::Register(names[i], IconID::AppBox, &AppFunctions::MakeScene<DummyScene>, args[i]);
        }
        eq(AppFunctions::FindByArg("/lua/apps/c/main.lua"), 2, "store: FindByArg");
        eq(AppFunctions::FindByArg("/lua/apps/x/main.lua"), -1, "store: FindByArg 無ければ-1");

        LuaPermissions perms;
        perms.network = true;
        check(AppFunctions::Refresh(2, "C2", perms, "/lua/apps/c/icon.pimg"), "store: Refresh");
        check(AppFunctions::Get(2)->name == "C2" && AppFunctions::Get(2)->permissions.network
              && AppFunctions::Get(2)->icon_file == "icon.pimg", "store: Refresh で名前・権限・アイコンが変わる");

        //並び: E D C B A にして、Dを隠す
        for(int i = 1; i < 5; i++) AppFunctions::MoveAppToFront(i);
        AppFunctions::SetHidden(3, true);
        check(AppFunctions::Unregister(2), "store: Unregister");
        eq(AppFunctions::Count(), 4, "store: 1つ減る");
        check(AppFunctions::Get(2)->name == "D", "store: 後ろが詰まる");
        eq(AppFunctions::FindByArg("/lua/apps/e/main.lua"), 3, "store: Eのindexが1つ減る");
        //見える並びは E B A(Dは隠れたまま)
        eq(AppFunctions::VisibleCount(), 3, "store: 見えているのは3つ");
        eq(AppFunctions::VisibleAt(0), 3, "store: 先頭はE");
        eq(AppFunctions::VisibleAt(1), 1, "store: 次はB");
        eq(AppFunctions::VisibleAt(2), 0, "store: 最後はA");
        check(AppFunctions::IsHidden(2) && !AppFunctions::IsHidden(3), "store: 隠した印も詰まる");
        check(!AppFunctions::Unregister(9), "store: 範囲外はfalse");
        //詰めた後に足しても並びが壊れない
        AppFunctions::Register("F", IconID::AppBox, &AppFunctions::MakeScene<DummyScene>, "/lua/apps/f/main.lua");
        eq(AppFunctions::VisibleAt(3), 4, "store: 足したものは最後");
        HostSd::files.erase("/sys/launcher.cfg");
    }

    // ===== 起動時に開く画面(LaunchByName(name, scene) / TakeLaunchScene) =====
    {
        AppFunctions::Clear();
        AppFunctions::Register("時計", IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
        AppFunctions::Register("テトリス", IconID::AppBox, &AppFunctions::MakeScene<DummyScene>,
                               "/lua/apps/テトリス/main.lua");
        FixedString<PICO_STR_S> scene;

        check(AppFunctions::LaunchByName("テトリス", "play"), "scene: 起動できる");
        check(!AppFunctions::TakeLaunchScene("/lua/apps/他/main.lua", scene), "scene: 別のアプリは受け取れない");
        check(AppFunctions::TakeLaunchScene("/lua/apps/テトリス/main.lua", scene) && scene == "play",
              "scene: argで受け取れる");
        check(!AppFunctions::TakeLaunchScene("/lua/apps/テトリス/main.lua", scene), "scene: 1回きり");

        check(AppFunctions::LaunchByName("時計", "alarm"), "scene: C++製アプリも起動できる");
        check(AppFunctions::TakeLaunchScene("時計", scene) && scene == "alarm", "scene: 名前で受け取れる");

        //受け取られないまま別の起動があったら持ち越さない
        AppFunctions::LaunchByName("テトリス", "play");
        AppFunctions::Launch(0);
        check(!AppFunctions::TakeLaunchScene("/lua/apps/テトリス/main.lua", scene), "scene: 次の起動で捨てる");

        //画面の指定なし・知らない名前
        AppFunctions::LaunchByName("テトリス", "");
        check(!AppFunctions::TakeLaunchScene("/lua/apps/テトリス/main.lua", scene), "scene: 空なら預けない");
        check(!AppFunctions::LaunchByName("無い", "play"), "scene: 知らない名前はfalse");
        check(!AppFunctions::TakeLaunchScene("", scene), "scene: 空のkeyはfalse");
    }

    AppFunctions::Clear();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
