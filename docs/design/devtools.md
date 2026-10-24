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
**実機では未確認**(このリモート環境にはRP2350のボード定義が無くビルドもできていない): `isr_hardfault`がarduino-picoで
差し替えられるか(既に定義していれば多重定義)・`.uninitialized_data`がarduino-picoのリンカスクリプトにあるか・
`watchdog_enable_caused_reboot()`の名前・RP2350(Cortex-M33)のnakedのアセンブリ・デバッガの画面の描画と`Font0`・
プロファイラの実機の値。

## 実機の確認用の道具 (`src/devtools/HwTest_Functions` / Luaアプリ「実機チェック」) (2026-10-11)

`docs/HARDWARE_CHECKLIST.md`(実機で確かめる・測る項目の一覧)を回すための道具。
- **シリアルのコマンド`hw ...`**: `/sys/debug.cfg`の`hw-test = true`のときだけ(既定は切れている。わざと落とすコマンドがあるため。設定アプリには出さない)。
  `hw info`/`hw mem`(`MemFunctions::Take()`と`HeapTopRoom()`)/`hw trap`(`__builtin_trap()`=未定義命令)/`hw buswrite`(0xF0000000へ書く)/
  `hw hang`(ウォッチドッグが入っているときだけ止まる)。結果は`[HW]`の行。`PadFunctions::extra_line_handler`はLuaデバッガ(`dbg`)→`hw`の順に試す
  (`main.cpp`のラムダ)。`HwTest_Functions.cpp`はmain.cppからしか参照しないのでホストテストはリンクしない。
- **「実機チェック」**(`pc/sdcard/lua/apps/実機チェック/`、`permission_network=true`): コルーチンで1フレームに1つずつ測る(Lua・renderの中の描画・
  全面の描き直し60フレーム・SD・暗号・JSON・HTTPS・メモリ)。結果は画面・`[HWCHK]`のログ・`results.txt`(追記、`.gitignore`済み)。
  1つの測定は命令数の上限(1回200万命令)とLuaの予算に収まる量にしてある(表への追加を1万件にしたらPCでLuaのメモリが足りなかった)。
- 検証: PCビルドで`hw info/mem/help`、`hw trap/buswrite/hang`→次の起動で`/crash/crash_NNNN.txt`と通知、「実機チェック」の「全部」(HTTPSはこの環境では
  出口のゲートウェイで証明書が差し替わるため失敗するのが正しい)、`PICOOS_VERIFY_LCD=1`で食い違い0、ホストテスト全部。**実機では未確認**(実機ビルドもこの環境ではできない)。

## ファームの更新 (`src/apps/firmware/` / `Firmware_Functions` / `FirmwareScene` / `script/firmware_tool.py`) (2026-10-16)

使い方・形式・TCPの取り決めは`docs/FIRMWARE_UPDATE.md`。利用者と決めた方針(2026-10-16): arduino-pico標準のOTA(PicoOTA+内蔵フラッシュのLittleFS、自動ロールバック無し=壊れたらBOOTSEL)/
LittleFSは1728KB / 今回の経路はSDとPCからのWi-Fi(Webから取るのは後) / 独自の見出し+gzip / **常に署名必須、鍵はファーム専用でPCにだけ置き、焼き込みの鍵だけを信頼**(SDの鍵は読まない。
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
- **鍵**: `src/apps/firmware/Fw_Keys.hpp`の`kBuiltin`は今は全部0(未登録)。利用者が`firmware_tool.py keygen`して1行を入れ、USBで1回書き込む必要がある(秘密鍵は`~/.pico-os/firmware.key`)。
  Python側の署名は`build_app_repo.py`のEd25519(RFC 8032)を使い回す。
- PC/Web: 置き場は`ota_emu/`(SDのルートの隣、`PICOOS_OTA_DIR`。`.gitignore`済み)、本体は書き換えない。ポートは`PICOOS_FW_PORT`。
- 検証: `firmware_test`(run.sh の core。見出しの検査10通り・署名(信頼/なし/知らない鍵/印と署名の食い違い/署名の後の書き換え)・版の確認・1バイトずつ途切れながら・置き場への書き込みの失敗・
  読み直しで化ける・gzipの末尾の大きさ・途中で切れる/取り消す/Doneの後に取り消す・前の残りを消す)、`firmware_tool_test.py`(偽の`firmware.bin`から作る・断る場合・**作ったものを`firmware_test`に読ませる**・
  pushのWAIT/OK/DONE/ERRを偽の本体で)、実機ファームのビルド(`board_build.filesystem_size`でLittleFSが`0x1024F000`〜、`picoOTA`がリンクされる、本物の`firmware.bin`を`fwpack`して`verify`)、
  PCビルドの`--tap`/`--shot`(SDから選ぶ→確認→書く→再起動→通知、`firmware_tool.py push`→本体で確認→DONE→再起動)。
  **実機でSDからもWi-Fiからも更新できることを利用者が確認した(2026-10-16)**。書き込みの時間は未計測。壊れたファームはBOOTSELで戻す。

## ナイトリービルド (`.github/workflows/nightly.yml`) (2026-10-24)

利用者の要望: 今のActionsに載せる / 実機ファームのビルドの成果物をそのまま落とせる / 50本ほど残して超えた分は消す。

- **ビルドは `firmware-build.yml` を `workflow_call` で呼ぶ**(手順を二重に持たない)。呼ばれた側は版(`serial`=`git rev-list --count HEAD`・`short`)を出力に出す。
  `concurrency` の組は `github.workflow`(呼ばれたときは呼び出し元の名前)を含むので、mainへのpushのビルドとナイトリーが互いを取り消さない。
- **置き場はActionsの成果物ではなくGitHubのリリース(プレリリース、`--latest=false`)**。成果物は最長90日で消え、zipに包まれ、落とすのにログインが要るため。
  添付は `firmware.uf2`/`firmware.elf`/署名なしの`firmware.pfw`(PRの成果物と同じ中身)。タグは `nightly-<通し番号>-<短いハッシュ>`(軽量タグ、`--target` はビルドしたコミット)。
- 毎日03:43(日本時間、`cron` は UTC 18:43)。**ナイトリーのタグがすでに今のmainを指していれば作らない**(手動実行の `force` で作り直す。同じタグは消してから作る)。手動実行はmainだけ。
- 説明は書き込み方の短い案内+`--generate-notes`(前のナイトリーのタグから。PRの一覧になる)。
- 作った後、`nightly-` のリリースを作った日時の新しい順に並べ、`KEEP`(50)本を超えた分をタグごと消す(`gh release delete --cleanup-tag`)。
- GitHub上で実際に回るかは未確認(初回はActionsタブから手動実行して確かめる)。
