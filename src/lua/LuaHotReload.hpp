#pragma once

#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

class BackgroundService;

// 実機でのLuaアプリのホットリロード(Wi-Fi転送。PCビルドの --hot-reload の実機版。docs/design/devtools.md)。
//
// /sys/debug.cfg の lua-hot-reload = true のときだけ:
//   - Wi-Fi転送の画面を閉じても待ち受けを続ける(この仕事の update() が FileShareFunctions::Update() を回す。
//     やめるのはバックグラウンドの一覧の[停止]。キーは画面を開いたときのまま)
//   - Wi-Fi転送で /lua/apps/<dir>/ の下の .lua か app.cfg が書き換わり、kQuietMs の間ほかに届かなければ、
//     そのアプリを開いているときは開き直す(同じアプリの push_scene したページは1フレームに1つずつ閉じてから、
//     登録簿の main.lua を Change で開く)。開いていなければ登録簿の名前・権限だけ読み直す
//   - Luaの状態だけを作り直す方式にしない理由は PCビルドの --hot-reload と同じ(タイマー・HTTP・画面の積み重ねを全部片付ける口が無い)
//
// 見るファイルを .lua と app.cfg に絞るのは、アプリ自身が書くセーブ等(Wi-Fi転送では来ないが、同じフォルダへ
// 置いた画像・データ)で開き直し続けないため。待つのは、ブラウザから複数のファイルを1つずつ送る間に開き直さないため。
namespace LuaHotReload {
    constexpr uint32_t kQuietMs = 600;

    // Wi-Fi転送が書き換えた/消した/動かしたパス(boot/ が FileShareFunctions::file_changed へ差す)
    void OnFileChanged(const char* path, uint32_t now_ms);
    // 静かになった書き換えがあれば、そのアプリのフォルダ(/lua/apps/<dir>)を out へ入れて忘れる
    bool TakeDue(uint32_t now_ms, FixedString<PICO_PATH_LEN>& out);
    // path が開き直す種類のファイルなら、そのアプリのフォルダを out へ(/lua/apps/<dir>/…/*.lua か /lua/apps/<dir>/app.cfg)
    bool AppDirOf(const char* path, FixedString<PICO_PATH_LEN>& out);

    // 有効か(DevToolsFunctions::LuaHotReload())。ホストテストの差し替え口
    inline bool (*enabled_query)() = nullptr;
    bool Enabled();

    // 最後に開き直したアプリ(一覧の状態の行)
    const char* LastReloaded();

    BackgroundService* Service();

    namespace Test {
        void Reset();
    }
}
