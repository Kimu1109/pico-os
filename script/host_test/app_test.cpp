// アプリ登録簿(AppFunctions)とランチャのタイル配置(AppGrid)を検証するテスト。
//
// AppGridはタイルごとにウィジェットを持たず、render()で直接描いてタップ位置から
// 対象を逆算する。つまり「描く位置」と「当たり判定の位置」が別々の計算になっていて、
// ずれても画面を見るまで気付けない。ここで両者の整合を突き合わせる。
#include "functions/App_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "gui/widgets/AppGrid.hpp"
#include "gui/scenes/Scene.hpp"
#include "OS_Data.hpp"
#include <cstdio>

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

static void registerApps(int n){
    AppFunctions::Clear();
    //名前は静的寿命が要るので固定のテーブルから配る
    static const char* kNames[] = {
        "a01","a02","a03","a04","a05","a06","a07","a08","a09","a10","a11","a12",
        "a13","a14","a15","a16","a17","a18","a19","a20","a21","a22","a23","a24",
    };
    for(int i = 0; i < n; i++){
        AppFunctions::Register(kNames[i], IconID::AppBox, &AppFunctions::MakeScene<DummyScene>);
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
            Scene* s = e->create();
            check(s != nullptr, "登録簿: 生成関数がシーンを返す");
            delete s;
        }

        check(!AppFunctions::Register(nullptr, IconID::AppBox,
                                      &AppFunctions::MakeScene<DummyScene>),
              "登録簿: 名前なしは拒否する");
        check(!AppFunctions::Register("x", IconID::AppBox, nullptr),
              "登録簿: 生成関数なしは拒否する");
        eq(AppFunctions::Count(), 1, "登録簿: 拒否された分は増えない");
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
        check(per_page >= 6, "1ページに6個以上並ぶ");
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

        //各タイルの中心を叩くと、そのタイルのindexが返ること。
        //描画と当たり判定が同じ矩形を使っているかの確認になる
        bool all_hit = true;
        for(int slot = 0; slot < per_page; slot++){
            //タイル配置はhitTileと同じ規則(行優先)で組み立てて中心を出す
            const int cols = 3, padding = 6, gap = 6, tile_h = 58;
            const int tile_w = (SCREEN_WIDTH - padding * 2 - gap * (cols - 1)) / cols;
            const int col = slot % cols, row = slot / cols;
            const int cx = padding + col * (tile_w + gap) + tile_w / 2;
            const int cy = padding + row * (tile_h + gap) + tile_h / 2;
            if(g.hitTile(cx, cy) != slot) all_hit = false;
        }
        check(all_hit, "各タイルの中心を叩くとそのindexが返る");

        eq(g.hitTile(-5, 10), -1, "左外は-1");
        eq(g.hitTile(10, -5), -1, "上外は-1");
        eq(g.hitTile(SCREEN_WIDTH + 10, 10), -1, "右外は-1");
        eq(g.hitTile(10, SCREEN_HEIGHT * 2), -1, "下外は-1");
        eq(g.hitTile(0, 0), -1, "外周の余白は-1(タイルの外)");
    }

    // ---- 空きスロットは起動しない ----
    {
        AppGrid g(0, STATUSBAR_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - STATUSBAR_HEIGHT);
        registerApps(1); //1ページに空きが大量に出る状態
        const int tile_w = (SCREEN_WIDTH - 6 * 2 - 6 * 2) / 3;
        eq(g.hitTile(6 + tile_w / 2, 6 + 58 / 2), 0, "先頭タイルは引ける");
        //2つ目のタイルの位置には何も登録されていない
        eq(g.hitTile(6 + (tile_w + 6) + tile_w / 2, 6 + 58 / 2), -1, "空きスロットは-1");
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
        const int tile_w = (SCREEN_WIDTH - 6 * 2 - 6 * 2) / 3;
        eq(g.hitTile(6 + tile_w / 2, 6 + 58 / 2), per_page, "2ページ目の先頭タイルのindex");

        check(g.prevPage(), "前ページへ戻れる");
        eq(g.hitTile(6 + tile_w / 2, 6 + 58 / 2), 0, "戻ると先頭アプリに戻る");
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

        const int tile_w = (SCREEN_WIDTH - 6 * 2 - 6 * 2) / 3;
        //2つ目のタイルの中心を押して離す
        OSData::touchX = 6 + (tile_w + 6) + tile_w / 2;
        OSData::touchY = STATUSBAR_HEIGHT + 6 + 58 / 2;
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

    // ---- Launch()が登録簿のシーンを作ってPushする ----
    {
        registerApps(2);
        AppFunctions::Launch(1);
        check(pushed_scene != nullptr, "Launch: シーンが生成されてPushされる");
        AppFunctions::Launch(99);
        check(pushed_scene != nullptr, "Launch: 範囲外indexでは何もPushしない");
    }
    delete pushed_scene;
    pushed_scene = nullptr;

    AppFunctions::Clear();
    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
