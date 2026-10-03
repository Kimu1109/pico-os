#pragma once

#include <stdint.h>
#include <stddef.h>

// クラッシュダンプの保存。
//
// 落ちた瞬間にはSDへ書けない(割り込み・壊れたヒープの中でSdFatを呼ぶのは危ない)ので、2段に分ける:
//   1. 落ちた瞬間: 電源を切らない限り消えないRAM(実機は .uninitialized_data = リセットで0クリアされない領域)へ
//      レジスタ・スタックの先頭・「今どこを処理していたか」(パンくず)を書き、再起動する
//   2. 次の起動: Setup() がそれを見つけたら /crash/crash_NNNN.txt へ文章として書き出し、印を消す。
//      PostPendingNotice() が通知を出す(タップでファイルビューワーが開く)
//
// 落ち方:
//   - HardFault(実機。不正なメモリアクセス・未定義命令等)。isr_hardfault を上書きして捕まえる
//   - ウォッチドッグ(実機。/sys/debug.cfg の watchdog = true のときだけ)。loop()が watchdog-ms(既定8000)
//     以上戻らなければハードウェアが再起動する。直前の処理はパンくず(SetPhase/画面/Luaアプリ)で分かる
//   - PCビルド: SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT をシグナルハンドラで捕まえ、記録を
//     <SDのルート>/sys/crash.pending へ書いてから落ちる(実機の「消えないRAM」の代わり)。ウォッチドッグは
//     スレッドで真似る(固まったら記録を書いて終了する)。Webは対象外
//
// Luaアプリのエラー(落ちはしない)も SaveLuaError() で /crash/lua_NNNN.txt へ残せる(1回の起動で
// kMaxLuaDumpsPerBoot 件まで)。LuaEngineからは LuaDebugger::SetErrorReporter() 経由で呼ばれる。
namespace CrashDumpFunctions {
    constexpr const char* kDir = "/crash";
    constexpr const char* kPendingPath = "/sys/crash.pending"; // PCビルドだけが使う
    constexpr uint32_t kDefaultWatchdogMs = 8000;
    constexpr int kMaxLuaDumpsPerBoot = 8;
    // SetPhase()の特別な値: フレームの仕事を終えて次のフレームを待っている(ProfilerFunctions::Section以外)
    constexpr uint8_t kPhaseIdle = 0xFE;

    enum class Kind : uint32_t {
        None = 0,
        HardFault = 1,
        Watchdog = 2,
        Signal = 3,      // PCビルド
        Test = 4,        // ホストテスト・動作確認用(TriggerTestCrash)
    };

    constexpr int kStackWords = 16;

    // 落ちた瞬間の記録。実機では消えないRAMに置くので、中身はPOD(コンストラクタを持たない)
    struct Record {
        uint32_t magic;
        uint32_t kind;          // Kind
        uint32_t signal;        // PCビルドのシグナル番号
        uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
        uint32_t sp;
        uint32_t exc_return;
        uint32_t cfsr, hfsr, mmfar, bfar;
        uint32_t stack[kStackWords];
        uint32_t stack_words;   // stack[]に入っている数
        // パンくず(落ちた時点で分かっていたこと)
        uint32_t uptime_ms;
        uint32_t frame_no;
        uint32_t phase;         // ProfilerFunctions::Section。0xFF=不明
        uint32_t in_lua;        // Luaの実行中だったか
        char scene[32];
        char lua_app[64];
        uint32_t checksum;
    };

    // パンくず。毎フレーム書く(実機では消えないRAMに置く。ウォッチドッグで再起動したときに読むため)
    struct Crumbs {
        uint32_t magic;
        uint32_t uptime_ms;
        uint32_t frame_no;
        uint32_t phase;
        uint32_t in_lua;
        char scene[32];
        char lua_app[64];
    };

    // 起動時(SD・ログの後)。前回の記録があればダンプを書いて印を消す
    void Setup();
    // 落ちたときに捕まえる仕組みを入れる(実機: 何もしない=isr_hardfaultはリンクで差し替わる。
    // PC: シグナルハンドラ)。ホストテストでは呼ばない
    void InstallHandlers();

    // ウォッチドッグ。ms=0で止める(実機はハードウェアの都合で一度動かすと止められないので、
    // 次の再起動まで有効のまま。止めるときは設定を変えて再起動する)
    void EnableWatchdog(uint32_t ms);
    bool WatchdogEnabled();
    // ウォッチドッグへ「生きている」を伝える。loop()の頭と、長く止まる処理(Luaデバッガの一時停止)の中で呼ぶ
    void Feed();

    // パンくず(毎フレーム)
    void BeginFrame(uint32_t frame_no);
    void SetPhase(uint8_t phase);
    // 今の画面の名前(Scene::getName())。ポインタが変わったときだけ写す
    void SetScene(const char* name);
    // Luaの実行に入る/出る。appは直近のLuaアプリ(app_dir)。nullptrなら変えない
    void SetLua(bool in_lua, const char* app);

    // 前回の起動でダンプを書いたなら、そのパスを返す(無ければnullptr)。通知はPostPendingNotice()
    const char* LastDumpPath();
    // 前回のクラッシュを通知する(NotificationFunctions::Setup()より後で1回)
    void PostPendingNotice();

    // Luaのエラーを残す(落ちてはいない)。appはapp_dir、traceはスタックトレース(複数行)。
    // SDが無い・1回の起動の上限を超えたら何もしない。書いたらtrue
    bool SaveLuaError(const char* app, const char* message, const char* trace);

    // ---- 以下はテスト・内部用 ----
    // 記録を文章にする(SDへ書く中身)。outが足りなければ切り詰める。書いた長さを返す
    size_t FormatRecord(const Record& r, char* out, size_t size);
    // 記録の整合(magicとchecksum)
    bool RecordValid(const Record& r);
    void SealRecord(Record& r);
    // ダンプを書く(連番のファイル名を探してoutへ返す)。書けたらtrue
    bool WriteDump(const char* prefix, const char* text, size_t len, char* out_path, size_t out_size);
    // 今のパンくずから記録を作る(落ちた瞬間に呼ぶ。SDには触らない)
    void FillRecordFromCrumbs(Record& r, Kind kind);
    // 記録を「次の起動で処理する」場所へ置く(実機: 消えないRAM、PC: crash.pendingファイル)
    void StorePendingRecord(const Record& r);
    // 動作確認用: その場で記録を作って保存し、落ちたのと同じ扱いにする(再起動はしない)
    void TriggerTestCrash();
}
