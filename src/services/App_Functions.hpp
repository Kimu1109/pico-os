#pragma once

#include "gui/icons/icons_data.h"
#include "lua/LuaPermissions.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class Scene;

// ランチャに並ぶアプリ1つぶんの情報。
//
// 名前と引数は`const char*`ではなく`FixedString`で**コピーして持つ**。
// 以前は静的寿命のリテラルしか渡せず、「SDを走査して見つけたLuaアプリを登録する」
// といった動的な登録ができなかったため。呼び出し側の文字列は寿命が短くてよい。
// AppEntry::icon_file の長さ(終端込み)
constexpr size_t kAppIconFileLen = 32;

struct AppEntry {
    // 表示名。ランチャは2行で12文字程度しか出せないので、48B(日本語16文字)あれば足りる
    FixedString<PICO_STR_M> name;

    IconID icon = IconID::AppBox;

    // 生成関数へ渡す引数。Luaアプリならスクリプトのパス、文書ビューアなら
    // 開くファイルのパスといった用途。使わないアプリは空のままでよい。
    // パス全長(PICO_PATH_LEN=255)ではなく96Bなのは、登録簿が固定長テーブルで
    // 常時RAMを占めるため(下のkMaxAppsのコメント参照)。
    FixedString<PICO_STR_L> arg;

    // SD上の.pimgをタイルアイコンとして使う場合の、argのディレクトリからの相対パス
    // (Luaアプリなら app.cfg の icon。たいてい "icon.pimg")。空文字なら上のiconを使う。
    // 絶対パス(96B)で持つと32件で3KBを常に占めるので、argと重なるディレクトリ部分は持たない。
    // 絶対パスは AppFunctions::IconPathOf() で組み立てる
    FixedString<kAppIconFileLen> icon_file;

    // このアプリ(主にLuaアプリ)に許す権限。C++製アプリは既定(両方false)のまま無視してよい。
    // 元はLuaScene生成側(App_List.cpp)が生成関数ごとに手書きしていたが、SDスキャンで
    // 見つけたアプリごとに設定ファイル(app.cfg)から読んだ値を渡せるよう、登録簿側へ
    // 一般化した(docs/design/lua.md「Luaバインディング」「権限」参照)
    LuaPermissions permissions;

    // シーンを1つ生成する。生成したシーンの所有権はSceneFunctionsへ渡る。
    //
    // 引数として自分自身(AppEntry)を受け取るので、**同じ生成関数へ別のargを持たせて
    // 「同じシーン型・違う中身」のアプリを何個でも登録できる**。
    // (Luaアプリが「同じLuaScene型 + 別スクリプトパス」で増えるのがこの形)
    //
    // std::functionではなく素の関数ポインタにしてあるのは、
    // 登録簿を静的テーブル(確保ゼロ)のままにしたいため
    Scene* (*create)(const AppEntry& entry) = nullptr;
};

// アプリの登録簿。
//
// 狙いは「アプリを1つ増やすときに触る場所を1箇所にする」こと。
// 以前は HomeScene がアプリごとのボタンをメンバとしてハードコードしていたため、
// アプリを足すたびに HomeScene の .hpp と .cpp の両方を編集する必要があり、
// 標準アプリ6個+セカンダリ7個を並べると破綻する形だった。
//
// 仕組み(Register/Launch等)は App_Functions.cpp に、
// 実際に載せるアプリの一覧は App_List.cpp の Setup() にある。
// アプリを増やすときは App_List.cpp へ1行足すだけでよい。
//
// 起動後に登録簿を書き換えることもできる(Luaアプリのスキャン等)。
// ただしランチャ(AppGrid)は再描画の指示までは面倒を見ないので、
// 表示中に増減させた場合は呼び出し側でAppGridへneedsRender()すること。
namespace AppFunctions {
    // 登録できるアプリ数の上限。固定長配列で持つので、超えた分は警告して捨てる。
    // AppEntry1件が約190B(名前48B + 引数96B + アイコンのファイル名32B + 権限2B + アイコン種別
    // + 関数ポインタ)なので、この配列だけで常時約6KBのstatic RAMを占める
    // (24件ではSDのLuaアプリと合わせて埋まったので、SSHアプリを足したときに32件へ、
    // 録音アプリを足したときに40件へ広げた)。上限や文字列長を増やすときはその点に注意すること
    constexpr int kMaxApps = 40;

    inline AppEntry apps[kMaxApps];
    inline int app_count = 0;

    // ランチャの並び(登録簿のindexの並べ替え。0..app_count-1 を1つずつ含む)と、隠したアプリの印(ビット=index)
    inline int8_t   launcher_order[kMaxApps] = {};
    inline uint64_t launcher_hidden = 0;
    static_assert(kMaxApps <= 64, "launcher_hidden のビット数が足りません");

    // 型Tのシーンを作るファクトリ。引数を使わないアプリ向け。
    // Register()へ &MakeScene<XxxScene> の形で渡す
    template<typename T>
    Scene* MakeScene(const AppEntry&){ return new T(); }

    // 型Tのシーンを entry.arg 付きで作るファクトリ。
    // Tは const char* を1つ取るコンストラクタを持つこと。
    // Register()へ &MakeSceneWithArg<XxxScene> と arg をセットで渡す
    template<typename T>
    Scene* MakeSceneWithArg(const AppEntry& entry){ return new T(entry.arg.c_str()); }

    // 登録に成功したらtrue。name/arg/icon_pathはこの場でコピーされる。
    // 以下の場合はfalse:
    //   - 名前か生成関数が未指定
    //   - 登録上限に達している
    //   - argが長すぎて切り詰められる(パスとして別物になるため登録ごと拒否する)
    // 名前のほうは切り詰めても表示が縮むだけなので、警告を出した上で登録は通す。
    // icon_pathは絶対パスで渡す。argのディレクトリの中にあり、そこからの相対パスが
    // kAppIconFileLenに収まるときだけ覚える(そうでなければargほど致命的ではない(アプリ自体は動く)ため、
    // 登録は拒否せず既定アイコンへフォールバックする)。
    // permissions/icon_pathは省略時の既定(両方false / アイコン無し)で、
    // 従来通りC++製アプリの呼び出し側は変更不要
    bool Register(const char* name, IconID icon, Scene* (*create)(const AppEntry&),
                  const char* arg = nullptr,
                  const LuaPermissions& permissions = LuaPermissions{},
                  const char* icon_path = nullptr);

    int Count();

    // argが一致するアプリの登録簿のindex(無ければ-1)
    int FindByArg(const char* arg);

    // 登録済みのアプリの名前・権限・アイコンを差し替える(アプリストアで更新したとき)。
    // ランチャの並び・隠した印はそのまま
    bool Refresh(int index, const char* name, const LuaPermissions& permissions,
                 const char* icon_path = nullptr);

    // 登録簿から外す(アプリストアで削除したとき)。後ろのアプリのindexは1つずつ詰まる。
    // ランチャの並び・隠した印も詰める(launcher.cfg は名前で持つので書き直さなくてよい)
    bool Unregister(int index);

    // 範囲外なら nullptr
    const AppEntry* Get(int index);

    // indexのアプリを起動する。
    // Pushで進むので、アプリ側からPop()すればランチャへ戻れる
    void Launch(int index);

    // nameで登録されたアプリを探してLaunch()する(完全一致)。
    // 見つからなければ何もせずfalseを返す(Lua側のpico.launch_appのように、
    // 名前の綴りミスを呼び出し側で検知できるようにするための戻り値)
    bool LaunchByName(const char* name);

    // argの親ディレクトリ(正規化済み)がdirに一致するアプリを探し、その名前をoutへ。
    // Luaアプリ(argがスクリプトのパス)を、そのディレクトリ(LuaEngineのapp_dir)から引く用途。
    // 通知の送り主→タップで起動するアプリ、の対応に使う(NotificationFunctions)
    bool NameForDir(const char* dir, FixedString<PICO_STR_M>& out);

    // タイルアイコンの.pimgの絶対パスをoutへ。icon_fileが無ければfalse
    bool IconPathOf(const AppEntry& entry, FixedString<PICO_PATH_LEN>& out);

    // 登録簿を空にする(主にテスト用。Setup()の冒頭でも呼ばれる)。ランチャの並びも初期状態へ戻す
    void Clear();

    // ===== ランチャの並び・非表示(/sys/launcher.cfg) =====
    // 登録簿のindex(Get/Launchに渡すもの)は登録した順のまま変えず、ランチャに見せる順番と
    // 隠したかどうかだけを別に持つ(並べ替えてもLaunchByName・通知の送り主の対応は変わらない)。
    // RAMは並び32バイト+隠した印4バイトだけ。
    //
    // launcher.cfg(無くてよい。上から順にランチャへ並ぶ。載っていないアプリはその後ろへ登録順に):
    //   app = 時計
    //   hidden = 辞書      ← 隠したアプリ(並びの中の位置は覚えておき、表示し直すとそこへ戻る)
    // アプリは名前で指す(同じ名前が2つあれば、ファイルの順に登録の若いものから当てる)。
    // 書き換えるのはランチャのタイルの長押しメニューと設定アプリ(その他タブの「ランチャ」)。
    // 隠したアプリも名前(pico.launch_app)や通知からは起動できる。

    // ランチャに見えている数と、見えている中で pos 番目のアプリの登録簿のindex(範囲外は-1)
    int VisibleCount();
    int VisibleAt(int pos);
    // 登録簿のindexのアプリがランチャの何番目か(隠している/範囲外なら-1)
    int VisiblePosition(int index);

    bool IsHidden(int index);
    int  HiddenCount();
    // 隠した中で n 番目のアプリの登録簿のindex(並びの順)。範囲外は-1
    int  HiddenAt(int n);

    // 以下は変わったらlauncher.cfgへ書き、変わらなかったら(端・範囲外)falseを返す
    // 見えているアプリの中で前(delta<0)/後ろ(delta>0)へ1つずつ動かす
    bool MoveApp(int index, int delta);
    bool MoveAppToFront(int index);
    bool SetHidden(int index, bool hidden);
    // 並びを登録順へ戻し、全部見せる(launcher.cfgを消す)
    void ResetLayout();

    // launcher.cfgを読む(Setup()の最後で呼ぶ。登録し直したときも)。無ければ登録順のまま
    void LoadLayout();
    bool SaveLayout();

    // このOSに載せるアプリをまとめて登録する(実装は App_List.cpp)
    void Setup();
}
