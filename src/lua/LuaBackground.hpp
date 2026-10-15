#pragma once

#include <cstddef>
#include <cstdint>

class LuaEngine;

// Luaアプリのバックグラウンドスクリプト(app.cfg の background)。
//
// アプリを閉じている間も、決まった間隔(background_interval 秒、既定900・10以上)ごとに
// **小さな予算の新しい LuaEngine を作ってスクリプトを1回動かし、終わったら捨てる**(使い捨て)。
// 画面のLuaアプリ(予算200KB)と同時にフルのエンジンを2つ持てないため、常駐はさせない。
// 回をまたいで覚えたいことは pico.store_save / settings_set / sd_write で持ち越す。
//
//   app.cfg:
//     permission_background = true      ← これが無ければ動かさない(既定は許さない)
//     background = background.lua       ← アプリのフォルダからの相対パス
//     background_interval = 900         ← 秒
//     (通知を出すなら permission_notify、通信するなら permission_network も)
//
// 1回の流れ: スクリプトを上から実行 → pico.http_request の応答や pico.after/every のタイマーが
// 残っていれば、毎フレームそれを進める → 何も残っていない/pico.finish() が呼ばれた/kMaxRunMs を過ぎたら
// エンジンを捨てる。次の回は「始めた時刻 + 間隔」。
//
// - 使える pico.* は画面を持たないもの(通知・通信・SD・保存・JSON・タイマー・暗号等。LuaEngine の
//   kBackgroundApi)。pico.create 等は nil。pico.is_background() が true を返す
// - エラーはダイアログを出さず、ログと一覧(設定アプリの「バックグラウンド」)に出す
// - 同時に動かすのは1本だけ。そのアプリが前に出ている間・SDが無い間・ヒープの残りが足りない間は延ばす
// - 予算は kBudgetBytes(実機で48KB)。超えれば「not enough memory」のエラーで止まるだけ
//
// 仕事(BackgroundService)の登録は Refresh() が登録簿(AppFunctions)の Luaアプリの app.cfg を読んで行う。
// 起動時(BackgroundFunctions::Setup())と、アプリストアでの入れ替え・USBドライブの後に呼ぶ。
namespace LuaBackground {
    constexpr int kMaxJobs = 8;
    constexpr uint32_t kDefaultIntervalSec = 900;
    constexpr uint32_t kMinIntervalSec = 10;
    // 1回に動いてよい時間(通信の応答待ちを含む)
    constexpr uint32_t kMaxRunMs = 30000;
    // 起動してから最初の回までの待ち(起動直後のWi-Fiの接続・NTPを待つ)
    constexpr uint32_t kFirstRunDelayMs = 30000;
    // 1つのスクリプトの上限(LuaScene と同じ)
    constexpr size_t kMaxScriptBytes = 16 * 1024;
    // Luaの予算(64bitのPC/ホストはポインタが倍なので LuaScene と同じ割合で広げる)
    constexpr size_t kBudgetBytes = sizeof(void*) >= 8 ? 48 * 1024 * 4 / 3 : 48 * 1024;

    // 登録簿のLuaアプリの app.cfg を読み直し、仕事を足す・直す・外す。仕事の数を返す
    int Refresh();

    // 今動いている回のエンジン(無ければnullptr。テスト・診断用)
    LuaEngine* RunningEngine();

    // 今前に出ている Luaアプリのスクリプトのパス(無ければnullptr)。Background_List.cpp が差す
    // (そのアプリが開いている間は、同じ store.json 等を書き合わないよう動かさない)
    extern const char* (*foreground_script)();

    namespace Test {
        void Reset();   // 仕事を全部外して捨てる
    }
}
