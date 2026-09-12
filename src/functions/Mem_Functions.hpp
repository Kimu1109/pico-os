#pragma once

#include <cstdint>
#include "consts.hpp"
#include "util/FixedString.hpp"

// ヒープの使用量と断片化を実測するための診断モジュール。
//
// 目的: シーン単位のメモリプール(アリーナ)を導入する前の「ステップ0」として、
//   (1) 本当に断片化しているのか、それとも単なるリークなのかを切り分ける
//   (2) アリーナの枠を何バイトで確保すればよいかを実測で決める
// この2点を数値で押さえる。計測せずに枠サイズを決めると、大きすぎればRAMの無駄、
// 小さすぎれば実機で確保失敗という形で跳ね返ってくる。
//
// 断片化とリークの見分け方:
//   - used が単調増加し続ける              -> リーク(解放漏れ)
//   - used は戻るのに max_alloc だけが縮む  -> 断片化(空きはあるが繋がっていない)
//   - free は潤沢なのに確保が失敗する       -> 断片化が実害を出している状態
//
// 計測自体を切りたい場合はビルドフラグで PICO_MEM_PROFILE=0 を指定する
// (全ての関数が空になり、loop()からの呼び出しごと最適化で消える)。
#ifndef PICO_MEM_PROFILE
    #define PICO_MEM_PROFILE 1
#endif

namespace MemFunctions {

    // シーン別統計を記録できるシーンの種類数。溢れた分は "(overflow)" にまとめる
    constexpr int kMaxTrackedScenes = 8;

    // 何回のシーン遷移ごとに自動でレポートを出すか。
    // 実機では「遷移を50回繰り返す」操作を手で行うので、途中経過が自動で出たほうが都合が良い
    constexpr int kAutoReportInterval = 10;

    // シーン破棄の即時ログを出す残留の下限値。
    //
    // residue(= used(破棄時) - used(onEnter前))はシーン滞在中の全時間を含む窓なので、
    // 背景サブシステム(Wi-Fi再接続/タスク/ログバッファ)がその瞬間に確保中だった分まで
    // 拾ってしまう。数百バイト程度で毎回警告を出すと本物のリークが埋もれるため、
    // 即時ログはこの閾値以上のときだけにする(累計はレポートのresidue列で見る)
    constexpr uint32_t kResidueLogThreshold = 1024;

    // largest_freeの探索上限。
    // 上限を設けないと、探索中のmallocがヒープ末尾を大きく伸ばして
    // スタック側の余裕を削ってしまう(伸ばした分は解放してもarenaに残る)。
    // システム内で最大の単一オブジェクトはMarkdownView(数十KB)なので、
    // 「64KB連続で取れるか」まで分かれば断片化の実害判定には十分
    constexpr uint32_t kMaxProbeBytes = 64u * 1024u;

    // ある瞬間のヒープの状態。
    // arena/used/free/free_blocks/keepcost は newlib の mallinfo() 由来、
    // largest_free は実際に malloc を試して求めた実測値
    struct Snapshot {
        uint32_t arena = 0;        // sbrkで確保済みのヒープ総量
        uint32_t used = 0;         // 使用中バイト数(ブロックヘッダ込み)
        uint32_t free_total = 0;   // 空きブロックの合計バイト数
        uint32_t free_blocks = 0;  // 空きブロックの個数。多いほど細切れ = 断片化の直接指標
        uint32_t keepcost = 0;     // ヒープ末尾にある解放可能な空き
        uint32_t largest_free = 0; // 既存の空きブロックから確保できる最大サイズ(実測)
        uint32_t stack_headroom = 0; // ヒープ末尾と現在のスタックポインタの間隔(0=計測不可)

        // largest_freeの実測中にヒープ自体が伸びてしまったか。
        // trueのときlargest_freeは「既存の空きから取れた量」を下回って見える可能性がある
        bool probe_grew_arena = false;

        // largest_freeがkMaxProbeBytesで頭打ちになったか。
        // trueなら「少なくともこのサイズは連続で取れる」の意味で、断片化は実害なしと見てよい
        bool largest_free_capped = false;
    };

    // 現在のヒープ状態を取得する。
    // probe_largest=true のときだけ malloc を試行して largest_free を実測する
    // (数十回の malloc/free が走るので毎フレーム呼ぶ用途には向かない)
    Snapshot Take(bool probe_largest = true);

    // 断片化率をパーミル(0-1000)で返す。1000 - largest_free/free_total * 1000。
    // 0 = 空きが1塊に繋がっている、1000に近い = 空きはあるが細切れで使えない。
    // 探索が上限で頭打ちになった場合(largest_free_capped)は実害なしとみなして0を返す
    uint16_t FragmentationPermil(const Snapshot& s);

    // --- ウィジェット本体の確保量 ---
    //
    // Widget::operator new/delete から呼ばれ、ウィジェットのオブジェクト本体だけを
    // 積算する。シーンアリーナ(Widget::operator newをアリーナへ差し替える方式)が
    // 抱えるのはこの部分だけなので、アリーナの枠はこの数字から決める。
    //
    // 注意: シーン全体のused増分(SceneStat::peak_bytes)にはウィジェット内部の
    // std::vector/std::functionも含まれる。それらはグローバルヒープに残り続けるため、
    // アリーナの枠の根拠にしてはいけない(倍近く過大になる)。
    void OnWidgetAlloc(size_t bytes);
    void OnWidgetFree(size_t bytes);

    // ホスト側の計測(script/host_test/mem_probe.cpp)がウィジェットの確保を
    // 横取りするためのフック。Widget::operator newはグローバルのoperator newを
    // 経由せずmallocを直接呼ぶため、これが無いとホストの確保カウンタから漏れる。
    // 実機では未設定(nullptr)なのでnullチェック1回ぶんのコストしかかからない
    inline void (*widget_alloc_observer)(size_t bytes, bool is_alloc) = nullptr;

    // 1行のログとして現在のヒープ状態を出す
    void Log(const char* label, bool probe_largest = true);

    // base からの増減を出す。「この処理で何バイト増えたか」を見るとき用
    void LogDelta(const char* label, const Snapshot& base, bool probe_largest = true);

    // 起動直後(グラフィック/SD初期化より前)のベースラインを記録する
    void Setup();

    // 常駐ウィジェット(Statusbar/キーボード)の確保が終わった時点を記録する。
    // ここまでの used が、将来アリーナを2分割する際の「常駐領域」に必要なサイズになる
    void SealPermanentBaseline();

    // メインループから毎フレーム呼ぶ。現シーン滞在中のピーク使用量を追う(mallinfoのみで軽い)
    void Update();

    // --- シーン遷移フック(SceneFunctionsから呼ばれる) ---
    //
    // 呼び出し順は 「OnSceneExit() -> BeforeSceneEnter() -> onEnter() -> AfterSceneEnter()」。
    // 最初のシーン(SceneFunctions::Setup)だけは退出が無いので後ろ3つのみ呼ばれる

    // シーン破棄(ClearSceneWidgets)の直後に呼ぶ。
    // 破棄しきったのに used がシーン開始前まで戻らなかった分をリーク候補として積算する
    void OnSceneExit();

    // 新シーンの onEnter() の直前に呼ぶ。ウィジェット生成前の used を基準値として控える
    void BeforeSceneEnter();

    // 新シーンの onEnter() の直後に呼ぶ。
    // ここで記録される増分が「そのシーンのウィジェットが必要とするバイト数」= アリーナ枠の根拠になる
    void AfterSceneEnter(const char* scene_name);

    // 蓄積したシーン別統計を表形式でログへ出す
    void LogReport();

    // シーン1種類ぶんの統計
    struct SceneStat {
        FixedString<PICO_STR_S> name;
        uint32_t enter_bytes = 0;   // onEnter()での増分の最大値(= ウィジェット生成に要した量)
        uint32_t peak_bytes = 0;    // 滞在中のピーク増分(ダイアログ等を開いた瞬間を含む)
        uint32_t widget_bytes = 0;  // うちウィジェット本体のピーク(= シーンアリーナが抱える量)
        uint32_t residue_bytes = 0; // 退出後に戻らなかった量の累計(リーク候補)
        uint32_t visits = 0;
    };

    inline SceneStat scene_stats[kMaxTrackedScenes];
    inline int scene_stat_count = 0;
    inline uint32_t transition_count = 0;

    // 起動直後 / 常駐確保後のスナップショット(レポートの基準に使う)
    inline Snapshot boot_snapshot;
    inline Snapshot permanent_snapshot;
    inline bool permanent_sealed = false;

    // --- ヒープ下限(シーン破棄直後のused) ---
    //
    // リークがあるかどうかの一次情報はこちら。
    // シーンのウィジェットが1つも生きていない瞬間なので、毎回ほぼ同じ値になるはず。
    // 遷移回数に比例して増えるなら本物のリーク、横ばいならシーン滞在中の
    // 一時確保を residue が拾っているだけ、と判断できる
    inline uint32_t floor_first_used = 0;
    inline uint32_t floor_last_used = 0;
    inline uint32_t floor_max_used = 0;
    inline uint32_t floor_samples = 0;

    // --- ウィジェット本体の確保量(Widget::operator newが積算する) ---
    inline uint32_t widget_live_bytes = 0;  // 現在生存しているウィジェット本体の合計
    inline uint32_t widget_live_count = 0;
    // 常駐ウィジェット(Statusbar/キーボード)ぶん。アリーナを分割する場合の永続領域
    inline uint32_t widget_permanent_bytes = 0;
}
