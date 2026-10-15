#pragma once

#include <cstdint>

#include "gui/icons/icons_data.h"
#include "util/FixedString.hpp"
#include "consts.hpp"

// バックグラウンドの仕事(画面を閉じても動き続けるもの)の1つ。BackgroundFunctions へ登録すると、
// main.cpp の loop() が毎フレーム update() を呼ぶ(今の画面が何であっても)。
//
// 2つの形がある:
//   - 止めるだけの仕事(toggleable()=false): ミュージックの再生。動いている間だけ active()、
//     一覧の[停止]で stop()
//   - 入り切りする定期の仕事(toggleable()=true): チャットの未読の見張り・Luaアプリのバックグラウンドスクリプト。
//     入っている間ずっと active()。入り切りは BackgroundFunctions::SetEnabled() が /sys/background.cfg に覚える
//
// 2コア目からは触らないこと(1コア目の loop() だけが呼ぶ)。
class BackgroundService {
    public:
        virtual ~BackgroundService() = default;

        // background.cfg のキーを兼ねる一意な名前("music" / "chat" / "lua:<フォルダ名>")
        virtual const char* key() const = 0;
        // 一覧に出す名前
        virtual const char* name() const = 0;
        virtual IconID icon() const { return IconID::Refresh; }

        // 毎フレーム呼ばれる(入っていないときも呼ばれるので、自分で何もせず戻ること)
        virtual void update(uint32_t now_ms) = 0;
        // 今動いている(定期の仕事なら入っていて、予定がある)。ステータスバーの印と一覧の色に使う
        virtual bool active() const = 0;
        // 一覧の2行目(「3/12 曲名」「次は 12:30」「最後: 失敗(理由)」等)
        virtual void status(FixedString<PICO_STR_LL>& out) const = 0;

        virtual bool toggleable() const { return false; }
        virtual bool enabled() const { return true; }
        // toggleable() のときだけ呼ばれる。覚えるのは BackgroundFunctions の仕事
        virtual void setEnabled(bool) {}
        // !toggleable() のときの[停止]
        virtual void stop() {}
        // 定期の仕事を待たずに今すぐ1回動かす(一覧の[今すぐ])。できなければfalse
        virtual bool runNow() { return false; }

        // 一覧から開くアプリ(登録簿の名前)。無ければnullptr
        virtual const char* appName() const { return nullptr; }
        // 動いている間に望む音の出力の周波数(0=何も望まない)。画面が望まないときに使われる
        virtual uint32_t preferredSampleRate() const { return 0; }
};

// バックグラウンドの仕事の登録簿(OS常駐)。
//
// - 登録は Background_List.cpp の Setup() に集めてある(アプリの登録簿 App_List.cpp と同じ形)。
//   Luaアプリの仕事は LuaBackground が app.cfg を読んで足し引きする
// - 入り切りの状態は /sys/background.cfg("<key> = on|off")。書いていなければ仕事ごとの既定
// - 確保はしない(固定長の配列。仕事の本体は登録した側が持つ)
//
// 中身(このファイル)は描画にも音にも依存しないので、ホストテスト(background_test)で確かめる。
namespace BackgroundFunctions {
    constexpr int kMaxServices = 16;

    // 登録する。同じキーが既にある/満杯なら false。background.cfg に書いてあればその入り切りを当てる
    // (default_enabled は書いていないときの既定。toggleable() でない仕事には使わない)
    bool Add(BackgroundService* s, bool default_enabled = true);
    void Remove(BackgroundService* s);
    int Count();
    BackgroundService* At(int i);
    BackgroundService* Find(const char* key);

    // 入り切りして background.cfg へ覚える(toggleable() の仕事だけ)
    void SetEnabled(BackgroundService* s, bool enabled);
    // background.cfg に書かれた入り切り(書いていなければ def)
    bool ConfigEnabled(const char* key, bool def);

    // 毎フレーム(main.cpp の loop()、サービスの最後のほう)
    void Update();
    void UpdateAt(uint32_t now_ms);

    // 何か動いているか(ステータスバーの印)
    bool AnyActive();
    // 印のアイコン。今だけ動いている仕事(toggleable() でないもの=ミュージックの再生)があればその icon()、
    // 定期の仕事だけなら IconID::Refresh、何も動いていなければ IconID::IconCount
    IconID IndicatorIcon();
    // 動いている仕事が望む音の出力の周波数(無ければ0)
    uint32_t PreferredSampleRate();
    // 登録・入り切りが変わるたびに増える(一覧の画面が作り直す合図)
    uint32_t Revision();

    // 組み込みの仕事を登録する(Background_List.cpp)。AppFunctions::Setup() より後に呼ぶ
    // (Luaアプリの仕事を登録簿から拾うため)
    void Setup();

    namespace Test {
        void Reset();   // 全部外す(ホストテスト用)
    }
}
