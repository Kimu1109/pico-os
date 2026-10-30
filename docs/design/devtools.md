# 開発者向けの道具・実機の確認・ファームの更新

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## 開発者向けの道具 (プロファイラ / Luaデバッガ / クラッシュダンプ / Luaサンドボックス) (2026-10-03)

設定は`/sys/debug.cfg`(`DevToolsFunctions`。無くてよく、全部既定で切れている)。設定アプリの「他」タブの
「フレーム時間を表示」「Luaデバッガ」「固まったら再起動する」でも切り替えられる(`perf-log`と`watchdog-ms`はファイルだけ)。

**プロファイラ(`Profiler_Functions`)とフレーム時間の表示(`widgets/systems/PerfOverlay`)**
- `loop()`の頭で`BeginFrame()`、各処理の後に`Mark(区間)`(**区間の終わりで呼ぶ**。直前のMarkからの時間をその区間へ足す)、
  `IdleWait()`の手前で`EndWork()`。区間は 入力/電源/シーン/画面更新/描画/タスク/Wi-Fi/サービス(`main.cpp`の並び順)。
  0.5秒の窓で平均・最大・休みを除いた仕事の時間・1フレームあたりのLuaの時間(`LuaEngine::ProtectedCall()`の一番外が
  `AddLuaMicros()`する。ヘッダだけで完結させてあるのでLuaEngineはProfiler_Functions.cppへ依存しない)を出す。
  直近60フレームは輪で持つ。全部固定長。無効の間は`BeginFrame()`/`Mark()`がパンくず(下)だけ書いて戻る。
- 表示は右下132x50pxの常駐オーバーレイ(トーストより上)。書き換えは窓が閉じたとき(0.5秒ごと)だけ(毎フレーム描くと
  表示そのものが測る時間を押し上げるため)。`hit_transparent`でタップは下へ素通り(ただし下の部品は隠れる)。
  文字はLovyanGFXの`Font0`(6x8)。グラフは緑≦16.7ms・黄≦33.3ms・赤。
- `perf-log = true`で5秒ごとに`[PROF] ...`をシリアルへ(SDのログには書かない)。`FlushDirty()`の旧来の`fps:`の行はそのまま。
- PCビルドの値は`main_pc.cpp`の`sleep_for(5ms)`を含むので、フレーム時間は約5ms+仕事になる。**実機の値は未計測**。

**Luaデバッガ(`src/lua/LuaDebugger` / `LuaDebugScreen`)**
- `LuaDebugger::GlobalEnabled()`(debug.cfgの`lua-debugger`)が立っている間に作られた`LuaEngine`だけが`LuaDebugger`を持つ
  (約3KB、`new`)。**今動いているエンジンには後から付けない**(次に開いたアプリから)。
- 止まる仕組みはLuaのフックの`LUA_MASKLINE`。行フックは重いので、ブレークポイントがある・ステップ実行中・`dbg pause`の予約が
  あるときだけ入れる(`LuaEngine::ApplyHook()`が`wantsLineHook()`を見る)。**フックはスレッドごと**なので、作った後の
  コルーチンへは`coroutine.resume`/`wrap`の包み(下)と、フックの中(`ApplyHook(L)`)で合わせる。
- 「止まる」= フックの中で`Frontend::onPause()`を呼んで戻るのを待つこと。OSの`loop()`はその間止まる(2コア目の音は鳴る)。
  実機/PCのFrontendは`LuaDebugScreen`: ウィジェットを使わず`OSData::lcd`へ直接描き、`PICO_Touch::Update()`/
  `PadFunctions::Update()`/`KeyInputFunctions::Pop()`/シリアルを自分で読む小さなループ(10msごと、ウォッチドッグへFeed)。
  続けるときは`OSData::frame`を液晶へ戻し、`RequestRedraw()`→次のフレームの`main.cpp`が`MarkDirtyBelow(全画面)`
  (半透明のダイアログの下も描き直すため。`MarkDirty()`だと止まる前の画面が残った)。**Webビルドは持たない**(メインスレッドを
  止められない)ので、止まる代わりにシリアルへ場所を出して続ける。
- 止まるきっかけ: 行のブレークポイント(16個、ファイル名は`MatchFile()`で後ろ一致)、`pico.breakpoint()`、ステップ
  (1行/次へ=深さ≦/抜ける=深さ<。`pico.breakpoint()`で止まったときはCの関数の段を引いて数える)、`dbg pause`、
  **捕まえられなかったエラー**(`LuaEngine::MessageHandler()`の中。続けてもエラーのまま進む)。`pcall`の中のエラーでは止まらない
  (Luaのpcallはメッセージハンドラを持たないため)。
- 変数はローカル(`(`で始まる中間値は除く)と上位値(`^名前`。トップレベルの`local`はこちらに出る)、計16個。値は
  `FormatValue()`で1行に(メタメソッドは呼ばない=Luaを動かさない)。ソースは`@パス`のチャンク名からSDのファイルを読む
  (`ReadSourceLines()`)。**そのため`LuaEngine::Run()`は`/`で始まるチャンク名に`@`を付ける**(エラーも`[string "..."]`ではなく
  `/lua/apps/x/main.lua:12:`の形になった)。`LuaScene`の`lib.lua`/`main.lua`はこれで足りる。
- シリアルのコマンド`dbg ...`は`PadFunctions`の行の振り分け(`extra_line_handler`。Pad_Functions.cppをLuaへ依存させないための
  関数ポインタ。`main.cpp`が`LuaDebugger::FeedSerialLine`を差す)で列(8件)へ入り、止まっていない間は`LuaScene::onUpdate()`の
  `engine->UpdateDebugger()`、止まっている間はFrontendが`pollSerial(L, true)`で処理する。シリアルの1行の上限`PadFunctions::kLineMax`は
  これのため32→96にした。
- Lua API: `pico.traceback([msg])` / `pico.breakpoint([msg])` / `pico.set_breakpoint(file, line)` / `pico.clear_breakpoint([file, line])` /
  `pico.debugger_enabled()`。ドキュメントは`lua-api-doc/content/api/system/debug.md`と`guide/app/debugging.md`。
- **スタックトレース**: 捕まえられなかったエラーは`MessageHandler()`が`BuildTrace()`(`main.lua:12 関数名`の行、10段まで)で
  `last_trace_`(1KB)へ作り、`ReportError()`がダイアログ(メッセージ+先頭4段)・ログ(全部)・`LuaDebugger::ReportError()`
  (=`CrashDumpFunctions::SaveLuaError`、`/crash/lua_NNNN.txt`)へ出す。デバッガが無くても常に効く。

**クラッシュダンプ(`CrashDump_Functions`)**
- 落ちた瞬間はSDへ書けないので2段: 消えないRAM(`.uninitialized_data`。pico-sdkのリンカスクリプトが0クリアしない領域)へ
  記録(`Record`: レジスタ・例外フレームの後のスタック16語・CFSR/HFSR/MMFAR/BFAR・パンくず、magic+FNV-1a)を置いて再起動 →
  次の起動の`Setup()`(`LogFunctions::Setup()`の直後)が`/crash/crash_NNNN.txt`へ文章にして書き、印を消す。
  `PostPendingNotice()`(通知の`Setup()`の後)が通知を出し、タップでファイルビューワーが開く(`file:`)。
- **HardFault**: pico-sdkのcrt0の弱いシンボル`isr_hardfault`を上書き(naked。`tst lr,#4`でMSP/PSPを選んで
  `pico_os_hardfault_c(frame, exc_return)`へ)。フレームのアドレスがSRAMの中のときだけ読む(壊れたSPで二重に落ちないため)。
  最後に`watchdog_reboot(0,0,0)`。CFSRの主なビット(IACCVIOL/DACCVIOL/IBUSERR/PRECISERR/STKERR/UNDEFINSTR/STKOF/
  UNALIGNED/DIVBYZERO)はダンプで日本語に読み解き、`arm-none-eabi-addr2line`の打ち方も書く。
- **ウォッチドッグ**(`watchdog = true`、`watchdog-ms`既定8000、1000〜8300): `watchdog_enable(ms, true)`。`loop()`の頭で`Feed()`。
  再起動の前に何も書けないので、`watchdog_enable_caused_reboot()`のときは**パンくず**から組み立てる。パンくず(`Crumbs`、
  これも消えないRAM)は毎フレーム: フレーム番号・時刻(`BeginFrame`)、今の区間(`SetPhase`。`ProfilerFunctions::Mark(s)`が
  **次の区間**を書く。`EndWork()`の後は「休み」)、画面の名前(`SetScene`、ポインタが変わったときだけ写す)、Luaの実行中か+
  直近のアプリ(`SetLua`。`LuaEngine::ProtectedCall()`の一番外が`LuaDebugger::NotifyActivity()`経由で呼ぶ)。
  **実機のウォッチドッグは一度動かすと止められない**ので、設定で切っても次の再起動まで効く。TLSのハンドシェイク(1〜2秒)・
  大きなSDの読み書きより長い同期処理があれば`watchdog-ms`を見直すこと。
- **PCビルド**: シグナル(SEGV/BUS/FPE/ILL/ABRT)のハンドラが記録を`<SDのルート>/sys/crash.pending`へ書いてから既定の動作で落ちる
  (`PICOOS_NO_CRASH_HANDLER`で無効)。ウォッチドッグはスレッドで真似る(固まったら記録を書いて`_exit(3)`)。Webは対象外。
  `crash.pending`は`<fcntl.h>`の`open()`ではなく`fopen()`で書く(ホストテストのSdFatスタブとO_*の値が食い違う罠を避けるため)。
- Luaのエラーは落ちていないが`/crash/lua_NNNN.txt`へ残す(1回の起動で8件まで)。
- **実機でダンプが残らなかった(2026-10-11、`hw trap`/`hw hang`とも再起動はした)**。原因は未特定(arduino-picoの`memmap_default.ld`には
  `.uninitialized_data (NOLOAD)`があるので、RP2350の起動の途中でRAMの先頭が上書きされる・ハンドラに来ていない等を疑っている)。
  控えとして**ウォッチドッグのスクラッチレジスタ0〜3**(ウォッチドッグの再起動で消えない。4〜7はpico-sdkが使う)に最小限を書く:
  `[0]=0x5043<<16|種類<<8|区間`、HardFaultは`[1..3]=pc/lr/CFSR`、ウォッチドッグ(有効なときだけ毎フレーム)は`[1..3]=時刻/フレーム(下位31bit)+in_lua(最上位)/画面の名前の番地`
  (`getName()`の文字列はフラッシュにあるので番地だけで再起動後も読める)。
  **2026-10-12の実機の結果: RAMの記録(0x20000184)は起動の途中で上書きされていた**(magicが番地のような値)。スクラッチの控えで`hw trap`/`hw hang`とも
  ダンプが残った。控えから作ったHardFaultのダンプは時刻・フレーム・pc/lr/CFSR以外のレジスタを「不明」として出す。
  RAMの記録が無ければこれから簡易版のダンプを書く(画面・Lua・スタックは無し)。起動時の診断(`BootInfo`: reason・RAMの記録の番地とmagic・
  パンくずのmagic・スクラッチ)をログへ出し、**`hw boot`**でも後から見られる(再起動の直後のログはUSBシリアルを開き直す前に流れて見えないため)。

**Luaサンドボックスの強化(`LuaEngine`)**
- **打ち切りは握り潰せない**: 命令数の上限で「打ち切り中」(`aborting_`)になり、フックを1命令ごとにして次の命令で投げ直す。
  `pcall`/`xpcall`/`coroutine.resume`/`coroutine.close`は包んであり(`l_guarded_call`、yieldをまたげるよう継続関数つき)、
  打ち切り中なら捕まえた結果を捨てて投げ直す。`coroutine.wrap`は自前(`l_wrap`)。打ち切りが解けるのは次の一番外の
  `ProtectedCall()`の入口だけで、**入れ子の呼び出し(`pico.set`から鳴るコールバック=`Dispatch`)では予算も打ち切りも積み直さない**
  (`call_depth_`。積み直すと、それを繰り返して上限を逃れられた)。
- **投げ方はメモリ不足(`LUA_ERRMEM`)**: 打ち切り中は`Alloc()`が増える確保を全部断り、`RaiseAbort()`がわざと確保して起こす。
  `lua_error()`だと`xpcall`のメッセージハンドラ(Luaの関数)が呼ばれ、**フックの中から投げるとそのハンドラはフック無し
  (`allowhook=0`)で動く**ので、ハンドラの中の終わらないループで固まった(テストで踏んだ)。メモリ不足はハンドラを呼ばない。
  表示は`ReportError()`が`abort_msg_`(場所付き)へ差し替え、トレースは打ち切りに入った時点でフックの中で作る。
- **ライブラリ**: `luaL_openlibs()`をやめ、基本/coroutine/table/string/math/utf8/osだけを開く(`openSandboxedLibs()`)。
  外したもの: `debug`(`debug.sethook()`で安全網のフックを外せた)、`io`/`package`/`require`/`dofile`/`loadfile`(権限の確認を
  通らずにSDへ触れた)、`os.exit`/`execute`/`remove`/`rename`/`getenv`/`tmpname`/`setlocale`、`string.dump`。
  `load`はモードを"t"に固定(書き換えたバイトコードはVMを壊せる)。`print`はログへ。
- **`__gc`を持つメタテーブルは`setmetatable`がエラーにする**。LuaはGCのメタメソッドをフック無しで動かすので、中の終わらない
  ループは止めようがなく、GCが走るところ(どこでも)でOSごと固まる。5.4ではsetmetatableの時点で`__gc`があったときだけ
  ファイナライザが付くので、そこで見れば足りる。
- **残っている限界**: C関数の中(重い`string.find`のパターン等)はフックが来ないので止められない → ウォッチドッグで拾う
  (PCビルドで`string.find(("a"):rep(40), ("a*"):rep(30).."b")`が2秒のウォッチドッグで落ちてダンプが書かれることを確認)。
  `LUAI_MAXCCALLS`(Cのスタック)は無改造のvendorのまま。
- 以前の`lua_engine_test`の「pcallで打ち切りを捕まえれば続けられる」は振る舞いが変わったので、「握り潰せない」へ書き換えた。

**検証**: `lua_sandbox_test`(run.sh。サンドボックス・打ち切り7通り+入れ子のコールバック+yieldをまたぐpcall+`__gc`・
スタックトレース・偽物のFrontendでのデバッガ一式)、`devtools_test`(run.sh。プロファイラの区間/窓/履歴・クラッシュダンプの書き出し/
crash.pending/壊れた記録/通知/Luaのエラー・debug.cfg)。PCビルド: 表示の`--shot`、`kill -SEGV`→次の起動でダンプと通知、
重いパターンでのウォッチドッグ、`pico.breakpoint()`とエラーで止まったデバッガの画面と「続行」(一時的に画面を書き出す
仕掛けを入れて撮った。コミットには含めていない)、設定アプリの切り替え。
**実機ビルドで確認済み(2026-10-19)**: `isr_hardfault`の差し替え(多重定義にならない)・`.uninitialized_data`がリンカスクリプトにある・
`watchdog_enable_caused_reboot()`の名前。デバッガの画面とプロファイラの`Font0`が実機で読めることも確認済み(2026-10-25)。
**実機では未確認**: RP2350(Cortex-M33)のnakedのアセンブリ(`hw buswrite`のダンプのPC/LRをaddr2lineで照合。Todoist参照)・プロファイラの実機の値。

## Luaアプリのホットリロードとテストランナー (`pc/HotReload_PC.hpp` / `script/lua_test/`) (2026-10-25)

アプリ作者がPCだけで「書く→見る→確かめる」を回すための2つ。使い方は lua-api-doc の `guide/app/testing.md`。

**ホットリロード(`picoos_pc --app 名前 --hot-reload` / `picoos pc dev`)**
- `--app` で開いたLuaアプリ(登録簿の `arg` が `.lua`)の親フォルダを `PicoOsSdHost::toHostPath()` でPCのパスにし、
  250msごとに下4段までの `.lua` と `app.cfg` の 名前・更新時刻・大きさ を混ぜた値を見る。変わって、次の回も同じ値なら
  `rebootSelf()`(電源メニューの再起動と同じ `execv`。同じ引数なので `--app` がまたそのアプリを開く)。
- **Luaの状態だけ作り直す方式にしなかった**: 画面の積み重ね(push_sceneしたページ)・タイマー・HTTP・画像・
  バックグラウンドの登録を全部片付ける口が無く、残ると「直したのに古い挙動」を見ることになる。PCの起動は1秒かからない。
- 見るファイルを `.lua`/`app.cfg` に絞るのは、アプリ自身が書く `store.json` 等で起動し直し続けないため。2回続けて同じ値を
  待つのは、エディタの「一時ファイルへ書いて置き換え」の途中で拾わないため。`--shot` のときは見ない。
- ネイティブ専用(Webはファイルを見張れない)。`src/` には手を入れていない。

**テストランナー(`picoos lua-test`)**
- `script/lua_test/pico_test.lua` が本体。`script/host_test/lua/pico_mock.lua`(既存の `pico.*` の偽物)の上でアプリを読み、
  テストファイル(`*_test.lua`)の `test(名前, 関数)` を順に動かす。確かめる関数は `ok/eq/ne/near/raises/fail`、
  アプリの操作は `load_app()` が返す `app` の `tap/fire/frame/run/key/back/widget/find_all`。
- 下請け `script/lua_test/pico_lua_test.c` を `cli-tool/picoos` が `cc` で直接作る(`script/lua_test/build/`、ソースが新しければ作り直す)。
  CMakeのホストテスト一式を作らずに済み、vendorしたLua(`LUA_32BITS`)なので**整数32bit・単精度の小数が実機と同じ**
  (母艦の `lua5.4` を使わない理由)。
- アプリは実機のサンドボックスに近い環境で動く: 標準の関数・ライブラリは白リストで写したもの(`io`・`dofile`・`loadfile`・
  `package`・`debug`・`os.exit`等・`string.dump` が無い)、`setmetatable` は `__gc` を断る、`load` はテキストだけでアプリの環境が既定、
  `require` は本物と同じ名前の決まりで `pico.*` → `src/lua/builtin/`、他はアプリのフォルダの `a/b.lua` → `a/b/init.lua`。
  `lib.lua` があれば `main.lua` より先(`LuaScene` と同じ)。アプリのグローバル変数はアプリごとの環境の表に入るので、テストの間で混ざらない。
- 偽物に無い `pico.*` は `pico_mock に無い関数です` で止める(nilを呼んだときの分かりにくいエラーの代わり)。`stubs` で足せる。
- `pico_mock.lua` に足したもの: `sd_root`(偽のSDの下に本物のディレクトリを読むだけで敷く。関数も可)・`sd_removed`・`launch_reason`。
  リポジトリの `pc/sdcard/` の下のアプリはSD全体をそこへ当て、それ以外のフォルダは `/lua/apps/<フォルダ名>` だけを当てる。
- テストは `tests/` と `*_test.lua` に置く決まりにし、`build_app_repo.py` は配る物から除く(端末に要らない)。
- 見本は `pc/sdcard/lua/apps/動作確認/tests/menu_test.lua`。検証は `pico_test_test`(lua-scene。`script/host_test/lua/test_app` の
  わざと失敗するものを含むテストで、数・失敗の文と行・サンドボックス・SDの下敷き・読み込みの失敗、と見本が通ること)。

## 実機の確認用の道具 (`src/devtools/HwTest_Functions` / Luaアプリ「実機チェック」) (2026-10-11)

`docs/HARDWARE_CHECKLIST.md`(実機で確かめる・測る項目の一覧)を回すための道具。
- **シリアルのコマンド`hw ...`**: `/sys/debug.cfg`の`hw-test = true`のときだけ(既定は切れている。わざと落とすコマンドがあるため。設定アプリには出さない)。
  `hw info`/`hw mem`(`MemFunctions::Take()`と`HeapTopRoom()`)/`hw trap`(`__builtin_trap()`=未定義命令)/`hw buswrite`(0xF0000000へ書く)/
  `hw hang`(ウォッチドッグが入っているときだけ止まる)/`hw clock [MHz]`・`hw sleep`(スリープ中のクロック低減を測る。`power-display.md`「スリープ中のクロック低減」)/
  `hw i2c`(I2C0のスキャン。SDA/SCLの電圧=プルアップの有無・応答するアドレス。0x52が見えたら暗号化なし初期化→0xFAのID6バイト(`..A4 20 01 01`=クラシック、`00 00`=ヌンチャク)と状態6バイトも出す。つないだ物が生きているかをWii本体なしで確かめる。2026-10-30。PCは「誰も居ない」を確認、実機では未確認(Todoist参照))。結果は`[HW]`の行。`PadFunctions::extra_line_handler`はLuaデバッガ(`dbg`)→`hw`の順に試す
  (`main.cpp`のラムダ)。`HwTest_Functions.cpp`はmain.cppからしか参照しないのでホストテストはリンクしない。
- **「実機チェック」**(`pc/sdcard/lua/apps/実機チェック/`、`permission_network=true`): コルーチンで1フレームに1つずつ測る(Lua・renderの中の描画・
  全面の描き直し60フレーム・SD・暗号・JSON・HTTPS・メモリ)。結果は画面・`[HWCHK]`のログ・`results.txt`(追記、`.gitignore`済み)。
  1つの測定は命令数の上限(1回200万命令)とLuaの予算に収まる量にしてある(表への追加を1万件にしたらPCでLuaのメモリが足りなかった)。
- 検証: PCビルドで`hw info/mem/help`、`hw trap/buswrite/hang`→次の起動で`/crash/crash_NNNN.txt`と通知、「実機チェック」の「全部」(HTTPSはこの環境では
  出口のゲートウェイで証明書が差し替わるため失敗するのが正しい)、`PICOOS_VERIFY_LCD=1`で食い違い0、ホストテスト全部。**実機では未確認**(実機ビルドもこの環境ではできない)。

## ファームの更新 (`src/apps/firmware/` / `Firmware_Functions` / `FirmwareScene` / `script/firmware_tool.py`) (2026-10-16)

使い方・形式・TCPの取り決めは`docs/FIRMWARE_UPDATE.md`。利用者と決めた方針(2026-10-16): arduino-pico標準のOTA(PicoOTA+内蔵フラッシュのLittleFS、自動ロールバック無し=壊れたらBOOTSEL)/
LittleFSは1728KB / 今回の経路はSDとPCからのWi-Fi(Webから取るのは後。下の「Webから取る」で2026-10-29に足した) / 独自の見出し+gzip / **常に署名必須、鍵はファーム専用で焼き込みの鍵だけを信頼**(SDの鍵は読まない。
SDへはLuaアプリも書けるため)/ 開発用に`debug.cfg`の`fw-allow-unsigned`(それでも毎回本体で確認)/ 古い版は確認のうえで許す / 更新後に通知し置き場を片付ける。

- **書き換えはarduino-picoのOTAのローダー**(`framework-arduinopico/ota/ota.c`。どのファームにもフラッシュの先頭12KBに入り、アプリは`0x10003000`から)。起動のたびにLittleFSの`otacommand.bin`を探し、
  あれば中身(gzipなら`uzlib`で解きながら)を4KBずつ写し(同じページは飛ばす)、終わると指示のブロックを消して再起動する。**途中で電源が落ちても次の起動で続きから**。本体は「中身を置いて指示を書く」だけ(`PicoOTA`)。
  `__FS_START__`/`__FS_END__`はイメージの`0x2FF0`/`0x2FF4`にあり、`firmware_tool.py`はそこから読んで見出しへ写す(今のファームと違えば本体が断る=割り当てを変えた版はUSBで書く)。
- **流れ(`Fw_Update`、描画・SD・ソケット・時計に依存しない)**: 見出し192バイト → `Fw_Image::Parse/Check`・署名(`VerifyBuiltin`。全部0の鍵は飛ばす)→ 確認(署名を確かめられない/古い/同じ版、
  またはSDから選んだとき)→ 置き場へ書きながらSHA-256 → 閉じて照合 → **読み直して**SHA-256・gzipの頭`1f 8b 08`・末尾の大きさ=見出しの`image_bytes`(アプリの領域に収まると確かめた値。
  ローダーは末尾の大きさだけ書くので、ここが違うとLittleFSを壊しうる)→ 控え(`pico_fw.note`)とローダーへの指示 → Done。失敗・取り消しでは置き場を全部消す。受け取り元(`FwSource`)・置き場(`FwStore`)は差し込み。
- **OSへのつなぎ(`Firmware_Functions`)**: SDのファイル / TCPの待ち受け(実機は`WiFiServer`、PCはPOSIXのソケット、Webは無し。画面で[受け取る]を押している間だけ、同時に1本、20秒無通信で切る)、
  電池(給電されていなくて30%未満なら断る)、中身を書き始める前に音を止める(LittleFSの書き込みは`idleOtherCore()`で2コア目を止めるため)、Doneから1.5秒後に`PowerMenuFunctions::RequestReboot()`
  (電源メニューの再起動と同じ後片付け)。`FwUpdate`は更新の間だけ`new`(約4.6KB)。起動時の`Setup()`(通知の後)が控えを読み、今の版と同じなら「更新しました」、違えば「更新できませんでした」を通知して置き場を消す。
  毎回LittleFSをマウントする(初めては自動でフォーマット。置き場は更新専用なので消えて困るものは無い)。
- **版(`Fw_Version`)**: `script/pio_firmware.py`(pre)が`$BUILD_DIR/pico_build_info/Pico_Build_Info.h`へ通し番号・短いハッシュ(手元の変更で`+`)・HEADのコミットの日時(UTC)を、変わったときだけ書く
  (`Fw_Version.cpp`だけが`__has_include`で読むので、毎回の全ビルドにならない)。同じ値を`PICOOS-BUILD;...`の文字列としてファームに置き、`firmware_tool.py`がそれを探して見出しへ写す。
  PC/Web/ホストテストは`#0 (dev)`。**CIの`firmware-build.yml`は`fetch-depth: 0`**(浅いクローンだと通し番号が小さくなる)にし、署名なしの`firmware.pfw`も成果物に入れた。
- **画面(`FirmwareScene`、ランチャの「本体の更新」。ファイルアプリで`.pfw`を開くと`FileViewerScene`から置き換わる)**: 今の版・鍵の登録の有無・状態・進み具合、[SDから選ぶ][受け取る][戻る/やめる]。
  確認は`MsgDialog`(確認が要らなくなったら閉じる)。更新の途中・待ち受け中・再起動待ちは`keepForeground`。離れるとやめる(再起動待ちのときだけはそのまま=電源メニューの再起動の`onExit()`)。
  ダイアログを開いたフレームより前に下の画面を書き換える(半透明のダイアログの下は描き直されないため)。
- **鍵**: `src/apps/firmware/Fw_Keys.hpp`の`kBuiltin`に利用者の鍵が1つ入っている(`firmware_tool.py keygen`で作り、USBで1回書き込む。秘密鍵は`~/.pico-os/firmware.key`)。
  当初は「PCにだけ置き、GitHubのSecretsにも置かない」としたが、**2026-10-29に利用者と見直し、Secrets(`FIRMWARE_SIGNING_KEY`)にも置いてナイトリーで署名する**ことにした
  (手元で毎回署名して上げるより圧倒的に手間が少ないため。下の「Webから取る」)。Python側の署名は`build_app_repo.py`のEd25519(RFC 8032)を使い回す。
- PC/Web: 置き場は`ota_emu/`(SDのルートの隣、`PICOOS_OTA_DIR`。`.gitignore`済み)、本体は書き換えない。ポートは`PICOOS_FW_PORT`。
- 検証: `firmware_test`(run.sh の core。見出しの検査10通り・署名(信頼/なし/知らない鍵/印と署名の食い違い/署名の後の書き換え)・版の確認・1バイトずつ途切れながら・置き場への書き込みの失敗・
  読み直しで化ける・gzipの末尾の大きさ・途中で切れる/取り消す/Doneの後に取り消す・前の残りを消す)、`firmware_tool_test.py`(偽の`firmware.bin`から作る・断る場合・**作ったものを`firmware_test`に読ませる**・
  pushのWAIT/OK/DONE/ERRを偽の本体で)、実機ファームのビルド(`board_build.filesystem_size`でLittleFSが`0x1024F000`〜、`picoOTA`がリンクされる、本物の`firmware.bin`を`fwpack`して`verify`)、
  PCビルドの`--tap`/`--shot`(SDから選ぶ→確認→書く→再起動→通知、`firmware_tool.py push`→本体で確認→DONE→再起動)。
  **実機でSDからもWi-Fiからも更新できることを利用者が確認した(2026-10-16)**。書き込みの時間はWi-Fi・SDとも30秒弱で、更新後の通知もどちらも届いた(2026-10-23)。壊れたファームはBOOTSELで戻す。

### Webから取る (`apps/firmware/Fw_Index` / `Fw_Download` / `script/build_fw_index.py` / `firmware-stable.yml`) (2026-10-29)

使い方・一覧の形式・CIの流れは`docs/FIRMWARE_UPDATE.md`「Webから取る」。利用者と決めた方針(2026-10-29): **署名の鍵はSecretsに置いてCIで署名**(上の「鍵」) /
一覧に載せるのは新しい安定版5本+ナイトリー3本 / 本体と同じ構成(env)の版だけを出す / 新しい版の通知はしない(「本体の更新」を開いたときだけ取る) / 書き込みの準備ができたら落としたファイルは消す。

- **置き場はGitHub Pages(`https://kimu1109.github.io/pico-os/firmware/`)**。元はリリースだが、リリースの添付は`release-assets.githubusercontent.com`への302で、
  飛び先に数百文字の署名付きクエリが付き、`Url::query`(192バイト)に入らない。飛び先のTLSのルート証明書も未確認。Pagesはアプリストアと同じホストで実機のHTTPSの実績がある。
  `web-pages.yml`は毎回サイトを丸ごと差し替えるので、公開のたびにリリースから`firmware.pfw`を落として写し直す。GITHUB_TOKENで作ったリリースでは他のワークフローが動かないので、
  ナイトリー・安定版のワークフローが最後に`gh workflow run web-pages.yml`する(`actions: write`)。
- **一覧には署名しない**。各`.pfw`の署名(焼き込みの鍵)を`Fw_Update`がいつも通り確かめるため、偽の一覧でできるのは古い署名済みの版を勧めることくらいで、それは「古い版は確認」で止まる。
  SHA-256は落としたものが壊れていないかを書き込みの前に見るため。
- **HTTPを`FwUpdate`へ直接流さず、一度SDの`/firmware/`へ落とす**。確認のダイアログで止まっている間に受信が止まりサーバに切られるのと、電池・署名・置き場の検査と確認を
  `StartFromSd()`のまま使えるため。代わりにSDへの書き込みと読み直しの分だけ長くなる。`.part`へ書き、大きさとSHA-256が一覧と合ったら名前を変える。
  置き場へ写し終えた(`FwUpdate::Done`)ら落としたファイルを消し、失敗・取り消しでは残す([SDから]で選び直せる)。
- **構成(env)**: `script/pio_firmware.py`が`PIOENV`を`PICOOS_BUILD_ENV`として書き、`FwVersion::Env()`と版の印(`env=`)に入る。`build_fw_index.py`は`.pfw`の中身をgunzipして印を読む
  (見出しの形式は変えない)。印に`env=`の無い古いファームは`rpipico2w`(ナイトリーは既定の構成しか配っていない)。PC/Webビルドは`rpipico2w`扱い(`PICOOS_FW_ENV`)。
- **後から署名**: `firmware_tool.py sign`は見出しの印を立てて署名を付けるだけ(見出しに中身のSHA-256があるので、作り直さずに付けられる)。`--expect-keys Fw_Keys.hpp`で
  鍵が焼き込みの公開鍵のどれとも違えば断る(本体が受け付けないものを配らないように)。ナイトリーはSecretsに鍵が無ければ署名なしで載せて警告、安定版は鍵が無ければ止める。
- **本体**: `Fw_Index`(段2、一覧の読み取り。16件・8KBまで、行は320バイトまで、10列目より後ろは読み飛ばす、ファイル名は英数字と`. _ -`で`.pfw`)、
  `Fw_Download`(段2、`HttpRequest`をkeep-aliveと無通信の打ち切り(`setIdleTimeout`)で使う。一覧8KBの溜め場所+16件で約13KB、TLSは別に約40KB。画面が開いている間だけ`new`)、
  `Firmware_Functions`の`StartWebIndex`/`StartWebFile`/`CloseWeb`(落とせたら`FwDownload`を消してTLSを返してから`StartFromSd`)。
  `FirmwareScene`は[Webから][SDから][受け取る]の3つ。一覧は`PickerDialog`のChoiceで「#523 夜間 10/28」(幅が狭いので「(今)」「(古)」「無署名」は日付の代わりに出す)。
- 検証: `fw_index_test`(core。値・1行目・構成と署名で外す・読めない行17通り・増えた列・ファイル名)、`fw_index_tool_test.py`(select・build・**作った一覧をC++に読ませる**)、
  `firmware_tool_test.py`に`sign`と`env=`、`run_net.sh`の`fw_download_net_test`(http.serverで一覧・1.5MBの取得・SHA-256/大きさ/404/取り消し/大きすぎる一覧/繋がらない)、
  PCビルドの`--tap`/`--shot`(`/sys/firmware.cfg`を手元のhttp.serverへ向けて、一覧→選ぶ→落とす→確認→置き場→落としたファイルを消す→再起動→通知、構成違いの表示)、
  `web-pages.yml`の一覧作りの手順を偽の`gh`で。**CIでの署名と本物のPagesの一覧は確認済み(2026-10-29。ナイトリー#665が署名済みで載り、Pagesの`.pfw`は焼き込みの鍵で`verify`が通る。ナイトリーからPagesの公開まで約16分)。実機でも、[Webから]で最新のナイトリーが一覧に出てダウンロード→書き込み→再起動まで通ることを利用者が確認した(2026-10-29)**。
- **Webのダウンロードのページ(`/download/`、`pc/web/download.html`)** (2026-10-30): 初めて書き込む人がリリースの一覧を探さずに済むよう、
  Pagesのトップと同じ見た目のページから`.uf2`/`.pfw`を落とせるようにした。`web-pages.yml`が`.pfw`と一緒に`firmware.uf2`も落とし、
  `build_fw_index.py build`が同じ名前の`.uf2`(拡張子だけ違う)と`downloads.json`(ページ用。版・種類・タグ・大きさ・SHA-256)を`/firmware/`に置く。
  **`index.tsv`の形式は変えない**(本体は`.uf2`も`downloads.json`も読まない)。同じ版が安定版とナイトリーにあれば安定版の行にナイトリーの`.uf2`を寄せる。
  ページは安定版の最新(おすすめ)と、それより新しいナイトリーをカードで、残りを表で出す。一覧が読めなければGitHubのリリースへのリンクだけ。
  `.uf2`は1本約4MBなので、載せる8本で30MB程度(Pagesの上限1GBに対して小さい)。
- CIで作った版の git に `+`(手元の変更あり)が付いていた(`firmware-build.yml` がプラットフォームの差し替えで `platformio.ini` を書き換えるため)。書き換えた後に `git update-index --assume-unchanged platformio.ini` して数えないようにした(2026-10-29)。


## ビルド時に選ぶハードウェア機能 (`src/config/Hardware_Config.hpp` の `PICO_HW_*`) (2026-10-25)

利用者の要望: 音声・マイク・電池を載せない機器では、最初からメモリを食うだけなので外せるように。液晶とSDは無いと動かないので選べなくてよい。

- **選べるのは `PICO_HW_SOUND` / `PICO_HW_MIC` / `PICO_HW_BATTERY`(既定は全部1=載せる)**。液晶・タッチ・SDは選べない
  (液晶の種類と画面の大きさは `PICO_PANEL_*` / `SCREEN_WIDTH`、`gui.md`「画面の大きさ」)。
  実機は env の `build_flags` に `-DPICO_HW_SOUND=0` 等(全部外した例が `env:rpipico2w_minimal`)、PCは `cmake -DPICOOS_HW_SOUND=OFF` 等。
- **外した機能は、実装の代わりに空の実装が入る**(`hal/Sound_Functions_Off.cpp`・`hal/Mic_Functions_Off.cpp`・`hal/Battery_Functions.cpp` の `#else`)。
  本物の `.cpp` は全体を `#if PICO_HW_*` で囲む。口(ヘッダ)は同じなので、呼び出し側(Lua・GB・動画・アラーム等、30か所ほど)は分岐しない。
  空の実装は「つながっていない」と答える: 音は `State::Disconnected`・効果音は捨てる・曲/WAVは理由付きで断る・`GbAudio()` は `nullptr`(エミュは音なしで動く)。
  マイクはメーターを頼むと `Input::Failed`・録音は `RecError::NoDevice`。電池は `HasSample()=false`・常にUSB給電(電池での音量の頭打ち・ファーム更新の残量チェックも掛からない)。
  設定ファイル(sound.cfg・mic.cfg)は読まない。
  - 呼び出しの口を `#if` で消す形にしなかったのは、呼び出し側の全部に `#if` が要り、外した版のビルドがすぐ壊れるため。
    空の実装の付け忘れ(ヘッダへ足した関数が `_Off.cpp` に無い)は外した版のリンクでしか分からないので、CI(`firmware-build.yml`)で `rpipico2w_minimal` も作る。
- **利用者に見える所からも消す**: アプリの一覧の「ミュージック」(音)・「録音」(マイク)(`boot/App_List.cpp`)、ステータスバーの音のアイコン、
  設定の音タブ(「音声出力なしのファームです」だけ出す)、設定の「電池で音量を絞る」(電池が無いとき)。電池の残量は `HasSample()=false` で元から出ない。
  動画・ゲームボーイは音なしで動くので残す。Luaは `pico.sound_available()` が false・`pico.battery()` が nil(前からある口)。
- **音声もマイクも外すと2コア目を起こさない**: `main.cpp` の `setup1`/`loop1` を定義しない(`PICO_HW_CORE1`)。arduino-picoはどちらも無ければ2コア目を起動せず、
  そのスタック(`core1_separate_stack` でヒープから取る8KB)も確保しない。PCビルドは2コア目の代わりのスレッドを止める。
- **`#if PICO_HW_*` を読むファイルは `config/Hardware_Config.hpp` を自分で include する**(未定義のマクロは `#if` で黙って0になる)。
  `script/check_layers.py` が `PICO_HW_` / `HardwareConfig::` を含むのに include していないファイルを報告する。画面の組み立ては `if constexpr (HardwareConfig::kSound)`。
- 大きさ(実機ファーム、2026-10-25): 全部外すと **静的RAM −5.3KB(131,288→125,948B)・フラッシュ −55.8KB(2,220,036→2,164,236B)**。
  ほかに、使い始めに取っていたヒープ(I2Sのバッファ2〜4KB・マイクのDMA 16KB・録音の列32KB・WAVの列等)と2コア目のスタック8KBが要らなくなる。
  既定の版の大きさは変わらない。
- 検証: `hw_off_test`(run.sh の media。外した定義で空の実装をリンクし、断り方と既定値を見る)、PCビルドで全部外した版の起動・設定の音タブ・アプリ数(39→37)、
  実機ファームの両方の env のビルド(外した版に `setup1`/`loop1`・`ChipSynth` が無いこと)。外した版も**実機で起動を確認済み(2026-10-25)**(空きヒープの差の数値は控えていない)。
- ファームの更新(`.pfw`)は版を区別しないので、載せる機能の違う本体へ送るとその機能が消える/増える(画面の大きさと同じ注意)。

## ビルド時に選ぶ内蔵アプリ (`src/config/App_Config.hpp` の `PICO_APP_*`) (2026-10-25)

利用者の要望: 内蔵のC++アプリをビルド時に有効化/無効化できるように。

- **選べるのは `apps/<機能>/` のアプリ**: `PICO_APP_BROWSER` `CALCULATOR` `CALENDAR` `CHAT` `DICT` `GB` `MAIL` `MUSIC` `NOTES` `READER` `SSH` `STORE` `TEXTEDITOR` `TODO` `VIDEO`
  (既定は1=載せる。`MUSIC` だけ既定が `PICO_HW_SOUND` で、音声出力なしに1を指定すると `static_assert` で止まる)。
  OSとして要るもの(設定・ファイル・時計・通知・USBドライブ・ホーム)と本体の更新(ファームを戻す道)は選べない。
  実機は env の `build_flags` に `-DPICO_APP_GB=0` 等(全部外した例が `env:rpipico2w_lite`)、PCは `cmake -DPICOOS_APP_GB=OFF` 等。
- **外すのは入口だけ。中身は `#if` で囲まず、リンカに捨てさせる**: arduino-picoは `-ffunction-sections` + `--gc-sections` なので、
  どこからも呼ばれない関数・静的な表は残らない。入口は次の4か所で、ここを `#if PICO_APP_*` で囲む。
  - アプリの一覧(`boot/App_List.cpp` の `Register`)
  - バックグラウンドの仕事(`boot/Background_List.cpp`: ミュージックの再生・チャットの未読・TODOの取り直し)
  - ホーム画面のウィジェット(`apps/home/Home_Widgets.cpp`: 音楽・予定・TODO。静的なオブジェクトは起動時に作られて残るので、クラスごと囲む)。
    描き方を差さなかった種類は追加の一覧に出ない。`home.cfg` に置いてあれば名前だけの枠になる(項目のメニューの「削除」で外せる)
  - ファイルからの関連付け(`FileViewerScene`・`FileExplorerScene`: `.pvid`→動画、Shift_JISの`.txt`と「読む」→読書、「編集」→テキスト)
  - アプリの `.cpp` 全体を囲む形(ハードウェア機能の本物の実装と同じ)にしなかったのは、アプリどうしが下回りを使い合うため
    (ホームの予定→`Ical`、読書→`Mail_Charset`、ストア/ファーム→`Ssh_Sha256`、メモ→テキストエディタ、ゲームボーイ/ファーム→`MusicService::Stop` 等)。
    囲むと組み合わせごとにリンクが壊れる。入口だけなら、残った使い手が要る分だけ残る(メモを残してテキストを外すと、タイルと「編集」だけが消える)。
- **`#if PICO_APP_*` を読むファイルは `config/App_Config.hpp` を自分で include する**(`script/check_layers.py` がハードウェア機能と同じく見張る)。
- 大きさ(実機ファーム、2026-10-25): 選べるものを全部外すと **静的RAM −3.4KB(131,288→127,772B)・フラッシュ −353.5KB(2,220,044→1,857,036B)**。
  既定の版の大きさは変わらない。入口を外しただけでリンカが中身を捨てていることの裏付けにもなっている。
- 検証: ホストテスト、PCビルドで全部外した版の起動とアプリ数(39→24。ランチャにはOSの6つと動作確認用のLuaアプリだけ)、
  実機ファームの `rpipico2w_lite` のビルド(CIでもPRとmainへのpushで作る)。外した版も**実機で起動を確認済み(2026-10-25)**。
- Luaの `pico.launch_app(name)` は外したアプリを名前で頼むと、登録簿に無いので false が返る(前からある振る舞い)。
- ファームの更新(`.pfw`)は版を区別しないので、載せるアプリの違う本体へ送るとそのアプリが消える/増える。

## ナイトリービルド (`.github/workflows/nightly.yml`) (2026-10-24)

利用者の要望: 今のActionsに載せる / 実機ファームのビルドの成果物をそのまま落とせる / 50本ほど残して超えた分は消す。

- **ビルドは `firmware-build.yml` を `workflow_call` で呼ぶ**(手順を二重に持たない)。呼ばれた側は版(`serial`=`git rev-list --count HEAD`・`short`)を出力に出す。
  `concurrency` の組は `github.workflow`(呼ばれたときは呼び出し元の名前)を含むので、mainへのpushのビルドとナイトリーが互いを取り消さない。
- **置き場はActionsの成果物ではなくGitHubのリリース(プレリリース、`--latest=false`)**。成果物は最長90日で消え、zipに包まれ、落とすのにログインが要るため。
  添付は `firmware.uf2`/`firmware.elf`/署名なしの`firmware.pfw`(PRの成果物と同じ中身)。タグは `nightly-<通し番号>-<短いハッシュ>`(軽量タグ、`--target` はビルドしたコミット)。
- 毎日03:43(日本時間、`cron` は UTC 18:43)。**ナイトリーのタグがすでに今のmainを指していれば作らない**(手動実行の `force` で作り直す。同じタグは消してから作る)。手動実行はmainだけ。
- 説明は書き込み方の短い案内+`--generate-notes`(前のナイトリーのタグから。PRの一覧になる)。
- **`firmware.pfw`は載せる前にSecretsの`FIRMWARE_SIGNING_KEY`で署名する**(2026-10-29。上の「Webから取る」)。鍵が無ければ署名なしで載せて警告、`Fw_Keys.hpp`と違えば止める。
  最後に`web-pages.yml`を動かしてWebの版の一覧を作り直す。
- 作った後、`nightly-` のリリースを作った日時の新しい順に並べ、`KEEP`(50)本を超えた分をタグごと消す(`gh release delete --cleanup-tag`)。
- GitHub上で回ることは利用者が手動実行で確認した(2026-10-29、署名つき)。
