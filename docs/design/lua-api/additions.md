# Lua API の設計記録(pico.*): まとめて足したAPI(Love2D比較・2026-10-05の追加)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `lua-api.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## Love2D比較で足した拡張API (2026-10-04)

Love2Dにあってpico-osのLua APIに無かったもののうち、C++側にほぼ実装があり橋渡しだけで済むものをまとめて足した。
ドキュメントは`lua-api-doc/content/api/`(graphics/drawing・graphics/images・media/sound・data/sdcard・system/misc)。**ハード制約で難しいもの
(半透明・Shader・Mesh・物理エンジン・スレッド・マルチタッチ)と、変換/座標変換のスタック(`translate/rotate/scale/push/pop`)・
`require`・OGG・複数WAVの同時再生・Quad/SpriteBatch・フォント差し替えは対象外のまま**
(MP3は2026-10-29に`pico.wav_play`で鳴らせるようになった。`sound.md`「MP3の再生」)。

- **図形**: `pico.draw_ellipse/fill_ellipse`、`draw_triangle/fill_triangle`、`draw_polygon/fill_polygon`(`{x1,y1,...}`の平らな配列、3〜32点。
  塗りは偶奇規則のスキャンラインで凹んだ形も塗れる)、`draw_arc/fill_arc`(ラジアン、0=右・時計回り。扇形は中心+円弧の多角形)。
  `draw_line`/`draw_triangle`/`draw_polygon`/`draw_arc`の末尾引数`width`(1〜64)は`CanvasRaster::DrawThickLine()`を再利用
  (**`drawWideLine()`は4bppパレットでクラッシュするので使わない**)。楕円は`drawEllipse/fillEllipse`をそのまま使う。
- **画像**: `pico.draw_image_ex(handle, x, y, r, sx, sy, ox, oy)`(Love2Dの`draw`と同じ並び。負の倍率で反転)。描き先の画素ごとに
  元画像を逆変換で引く最近傍で、**クリップ(`getClipRect()`)の内側だけ**を`writePixel()`する(実機のクリップ未設定時は全面を返す。
  ホストのスタブは未設定だと0を返すので、テストでは`setClipRect()`してから呼ぶ)。透過画像はindex0を飛ばす。
  **2026-10-06に速い道を足した**: 元と描き先がどちらも4bpp(回転0)なら`getBuffer()`を直接読み書きし、各行で「元画像の中に入る区間」を
  割り算で先に求めて外接矩形の空きを回らない(1画素ごとは32bitの加算だけ)。PCビルドで回転・2.5倍の描画が約2.3倍速く、出力は同じ。
  ホストテストのスタブ(1画素1バイト、`getColorDepth()`=8)は従来の`readPixelValue()`/`drawFastHLine()`の道を通る
- **回転済みのコマ**(2026-10-06): `pico.image_rotate(handle, frames, {sx,sy,ox,oy,start})`が`frames`通りの角度に回した絵を
  1枚の透過画像(一辺`cell`の正方形のコマを格子に並べたもの、スロットを1つ使う)へ先に焼き、`pico.draw_rotated(sheet, x, y, r)`が
  一番近い角度のコマを中心が(x,y)に来るよう写す(角度は丸まる。品質より速さ)。コマの情報は`ImageSlot::rot_frames/rot_cols/rot_cell`
  (解放で0へ戻す)。**写すのはLovyanGFXの透過つき`pushSprite()`ではなく4bppバッファどうしの直接コピー**
  (透過つきの`pushSprite()`は1画素ごとに色変換と判定を通り、PCビルドで同じ大きさの不透明な画像の約40倍遅かった)。
  PCビルドで1.5倍・16コマを180回: `draw_rotated` 2.4ms / 同じ大きさの透過画像の`draw_image` 8.9ms / `draw_image_ex` 5.5ms。
  出力はpushSpriteの道と同じ(PCビルドで比べた)。焼いた画像は0番が透過になる(元が不透明でも黒が抜ける)。
  実機では未計測
- **画像の描画は全部4bppの直接コピー**(2026-10-06): `IconRender::Blit4bpp(src, sx, sy, w, h, dx, dy, transparent, flip_x, flip_y)`が
  frameの今のクリップの内側だけを`getBuffer()`どうしで写す(元と描き先の画素の上位/下位が揃えば不透明は`memmove`・透過は1バイト=2画素ずつ、
  奇数ぶんずれていても隣の2バイトの4bitを組み合わせて1バイトずつ。反転は1画素ずつ)。`DrawPimgSprite()`がこれを先に試すので、
  `draw_image`・`Image`ウィジェット・`draw_tilemap`(タイルごとにクリップを触らず直接)・`draw_image_part`(反転も)・`draw_rotated`が全部これを通る。
  4bppでない(ホストテストのスタブ)ときはfalseで従来の`pushSprite()`へ落ちる。PCビルドで200回: 不透明`draw_image` 1.71→0.58ms・
  透過`draw_image` 1.57→0.50ms・`draw_image_part`(反転込み) 3.17→0.85ms・`draw_rotated` 5.74→1.71ms。出力は従来の道と全画素一致(PCビルドで比べた)。
  `ImageView::render()`も同じ`Blit4bpp`で写す(背景の塗りは下の`Fill4bpp::FillRect`)。実機では未計測
- **図形の塗りも4bppへ直接**(2026-10-06、`src/gui/Fill4bpp.hpp`、ヘッダのみ): `Fill4bpp::Span`がframeのバッファ・1行のバイト数・クリップを図形1つにつき
  1回だけ求め、横線は「端の半端な画素+間は`memset`」で書く。`FillRect/HLine/FillCircle/FillEllipse/FillTriangle`は**LovyanGFXの
  `fillRect`/`fillCircle`(+`fillCircleHelper`)/`fillEllipse`/`fillTriangle`と同じ手順を写した**もの(どの横線を塗るかが同じなので画素は変わらない。
  3点が一直線の三角形だけLovyanGFXの`drawLine`へ任せる)。`pico.fill_rect/clear_rect/fill_circle/fill_ellipse/fill_triangle`と`FillPoly`
  (`fill_polygon`/`fill_arc`)の横線が通る。4bppでない(ホストテストのスタブ)ときはfalseを返し、呼び出し側が従来のLovyanGFXの関数で描く。
  PCビルドで大きい図形を300回: 円1.5→1.1ms・楕円1.6→1.2ms・三角形3.3→2.1ms・多角形/扇形は約1.15倍(交点の浮動小数点の計算が大半)・
  長方形は同等(LovyanGFXの4bppの`fillRect`は元々バイト単位。`rect()`も同じく列ごとに端→`memset`→端の順にしてある)。小さい図形はLuaの呼び出しが大半で差は小さい。
  出力は従来の道と全画素一致(PCビルドで比べた)。実機では未計測
- **文字幅**: `pico.text_width(text[, font_size])`(`Label::GetTextWidth()`を新設。`DrawPlain()`と同じフォント設定で`textWidth()`)。
- **WAV**: `wav_pause/wav_paused/wav_position/wav_duration/wav_seek`(`SoundFunctions`にあったものを出しただけ)。
- **システム**: `pico.millis()`(単調)、`pico.battery()`(残量・電圧・USB給電。読めていなければnil。`LuaEngine.cpp`が`Battery_Functions.hpp`を取り込む)。
- **ファイル**: `pico.sd_stat(path)`→`{size,is_dir}`、`pico.sd_read_part(path, offset, length)`(`sd_read`と同じ16KiB上限)、`sd_list`の要素へ`size`を追加。
  **更新日時は持たない**(PCビルドの`FsFile`互換層に`getModifyDateTime`が無いため。要るなら互換層から足す)。
- **物理キーボード**: `pico.on_key(fn)`。`LuaScene::onKey()`→`LuaEngine::DispatchKey()`が`fn(key, mods)`を呼び(文字はUTF-8、特殊キーは名前、
  `mods={ctrl,alt,shift}`)、**真を返せば取った扱い、偽ならオンスクリーンキーボードの入力へ回る**。エラーになったらダイアログを1回出して以降は呼ばない
  (`loop()`と同じ安全弁。その打鍵は消費した扱い)。`ProtectedCall()`は戻り値を1つ受けるため`nresults`引数を足した。
  配り順は「画面の`onKey()`→開いているキー盤」(「物理キーボード」参照)で、日本語キー盤が読みを入力中のときはキー盤が先。
- 動作確認は「動作確認」アプリの「直接描画」(`pico-os-apps/apps/api-test/app/draw.lua`。図形・太線・画像の拡大/回転/反転・扇形・右寄せ文字・`on_key`・電池を1画面に描く。PCビルドの`--tap`+標準入力の`key`行で確認済み)。
- 検証: `lua_engine_test`(run.sh。dirty矩形・引数エラー・塗りのピクセル(正方形/L字/扇形)・画像の等倍/拡大/反転/回転/原点/透過・
  文字幅・WAV・battery・sd_stat/sd_read_part・on_keyの登録/解除/エラー)。PCビルドは通る。**実機・PCビルドでの見た目は未確認**
  (`fill_polygon`の1画素ごとの`drawFastHLine`、`draw_image_ex`の全画素ループの速さは実機で見ること。クリップが全面だと最大240x320回)。

## Lua APIの追加(2026-10-05): require・タッチ座標・画面の受け渡し・HTTP/JSON・タイマー

「ウィジェットを使うLuaアプリを書くときに足りない機能」を洗い出した(`pico.*`の不足リスト)うち、実害が出ていた5項目をまとめて入れた。
ドキュメントは`lua-api-doc/content/api/`(`system/modules.md`/`data/json.md`/`system/timers.md`/`ui/scenes.md`/`data/network.md`)、動作確認は「動作確認」アプリの「画面とデータ」(`pico-os-apps/apps/api-test/app/scene.lua`)。
**実機では未確認**(PCのホストテストとPCビルドのみ)。

1. **`require(name)` / `pico.require(name)`**(`LuaEngine::l_require`、`preloadModules`)
   - アプリのフォルダ(`app_dir_`)の`<名前>.lua`→`<名前>/init.lua`を読む。`.`はフォルダ区切り、`..`や`/`は不可、`sd_outside_app_dir`があっても外は探さない。
     1ファイル32KiBまで(`main.lua`は`LuaScene::kMaxScriptBytes`=16KiBのまま)。結果は`registry.pico_loaded`に覚え、2回目は同じ値。循環はエラー。
   - **実行中にコンパイルしない**のが肝(`LuaScene.hpp`のコメント: 実機のコア0スタックは4KiBで、Luaの実行中にパーサーを重ねて溢れた事故がある)。
     `Run()`がソースの`require("名前")`/`require "名前"`/`require('名前')`を**文字列として拾い**(`ScanRequires`)、本体より前に`PreloadTrampoline`(`ProtectedCall`越し)で
     読み込んで`registry.pico_preload[名前]`に関数(構文エラーなら文字列)で置く。読んだモジュールの`require`も辿る(最大16個)。実行時の`require`はその関数を呼ぶだけ。
     先読みで拾えない名前(変数で組み立てたもの)は、`lua_getstack(L, 6)`が無い=呼び出しが浅いときだけ実行中に読む。深ければ「関数の奥から呼ばれています」でエラー。
   - `package`ライブラリは引き続き無い。グローバル`require`は`pico.require`と**同じ関数**。サンドボックスのテストは「標準のrequireは無い」から「requireは自前のもの」へ書き換えた。
     モジュールの実行は`lua_pcall`で包むが、失敗は必ず再度`lua_error`で投げ直す(打ち切り=`aborting_`を握り潰さない)。
2. **タッチのイベント引数**: `press_start/move/end/out`は`fn(id, x, y, lx, ly, dx, dy)`(`Dispatch()`で組み立てる)。`x,y`=画面座標、`lx,ly`=`getScreenRect()`の左上からの座標、
   `dx,dy`=前のタッチのイベントからの移動量(`last_touch_x_/y_`。`press_start`は0)。`fn(id)`の書き方は従来どおり動く。`pico.get_touch()`は残してある。
3. **画面をまたぐ受け渡し**(`LuaScene`+`LuaEngine`)
   - `push_scene/change_scene(path, args)` → 子の`pico.args()`。`pico.pop(result)` → 親の`on_result(result)`。値はJSONにして**1KiB未満**(`EncodeSceneValue`、超過・JSON化不能は`luaL_error`)。
   - `LuaScene`は`launch_args`/`parent_script`/`saved_state`(2KiB)をメンバに持つ(`setLaunchArgs()`)。`LuaEngine`は`SetScriptPath()`/`SetSceneArgs()`で受ける。
     `change_scene`は親を引き継ぐ(置き換えた先の`pop(result)`は元の親へ届く)。
   - **結果の待ち箱**は`LuaScene::PostResult(target, json)`/`TakeResult(target, out)`(宛先=親のスクリプトパス、1件、`kResultTtlMs`=3秒で失効、別の画面宛ては取らない)。
     通知の起動理由(`TakeLaunchReason`)と同じ作り。親は`Pop`で戻って`onEnter()`からやり直すので、`setup()`の後に受け取る。
   - **状態の持ち越し**: `onExit()`で`engine->CallSuspend()`(グローバル`on_suspend()`が返したテーブルをJSON化して`saved_state`へ。エラーはダイアログを出さずログだけ)→
     次の`onEnter()`で`setup()` → `on_resume(state)` → `on_result(result)`の順に`CallWithJson()`で呼ぶ。`Push`で退避中もシーンのオブジェクトは残るのでメンバで持てる。
   - **`pico.store_load()`/`store_save(tbl)`**: `<app_dir>/store.json`(JSON、16KiBまで、一時ファイル→差し替え)。アプリを閉じても残る。`.gitignore`済み。
4. **HTTPとJSON**
   - `http_request(..., callback, opts)`: `opts.headers`(`HttpRequest::addExtraHeader()`で足す。`Host`/`Content-Length`/`Content-Type`/`Connection`/`Transfer-Encoding`等は不可、
     CR/LF・制御文字は不可、合計480B)、`opts.save_to`(`<path>.part`へ直接書いて最後まで受け取れたら差し替え。上限8MiB、`SdWriteAllowed`を通る=`app.cfg`や`app_dir`の外は不可。
     保存中は`setIdleTimeout(true)`で「10秒無通信」まで続け、`UpdateHttp()`が1フレームに最大16回・6msまで`update()`を回す)。
     `callback(ok, status, body, err, headers, info)`: `headers`は小文字キー(`content-type`/`content-length`/`etag`/`last-modified`/`location`。`HttpResponse::contentType()`を足した)、
     `info={size=, saved=}`。
   - **`opts.keep_alive = true`(2026-10-14)**: `HttpRequest::setKeepAlive()`を要求ごとに当て、同じ相手なら接続とTLSのハンドシェイクを使い回す
     (`info.reused`、`pico.http_close()`で閉じる。keep_aliveでない要求を始めると閉じる)。合わせて`HttpRequest`は**本文の無いPOST/PUT/PATCHにも
     `Content-Length: 0`を送る**(SpotifyのWeb API等が411を返すため。ChatClient/TodoistClientにも効く)、`HttpResponse::canReuseConnection()`は
     長さの無い204/304でも使い回せる。検証は`lua_engine_test`(ローカルサーバをkeep-alive対応にし、接続の数・411にならないこと・黙って閉じられた接続の繋ぎ直し)。
   - **Web APIの補助(2026-10-14、`src/lua/LuaEngine_Net.cpp`)**: `pico.form_encode(tbl)`(名前順・空白は`+`・配列は同じキーを繰り返す)、
     `pico.json_pick(text, paths[, {keep_null=}])`/`pico.sd_json_pick(path, paths)`(`util/Json_Reader`のSAXで流しながら読み、
     `"item.artists.1.name"`(配列は1始まり)・`"item.artists#"`(要素の数)の値だけを`{[パス]=値}`で返す。作業場所はLuaのuserdata(OOMでも漏れない)。
     文字列は255バイトで切り詰めて2番目の戻り値で知らせる。sd_json_pickは1MiBまで・全部見つかったら残りを読まない)。
     **追加ヘッダの上限を合計1000バイト・1行512バイト未満に広げた**(`HttpRequest::extra_header_`を1KiBに、1行は`kMaxHeaderLineBytes`。
     Bearerトークン数百文字のため。`addExtraHeader()`は先に長さを数えてから足す=大きな一時領域をスタックに取らない)。
     ホストテストの`LuaEngine`をリンクする5か所に`LuaEngine_Net.cpp`・`Json_Reader.cpp`・`Jpeg_Codec.cpp`を足した。
   - **同時に走るのは1本のまま**(RAMとTLSの約40KBのため。本物の並列にはしていない)。2本目以降は`PendingHttp`(本文・ヘッダを`std::string`で持つ)として最大`kMaxHttpQueue`=4本待たせ、
     終わるたび`StartQueuedHttp()`が先頭から始める。戻り値は`true`ではなくリクエストID(整数)に**変えた**(旧: 進行中に呼ぶと`false`、今: 順番待ち)。`http_cancel(id)`/`http_cancel()`(全部)。
     始められなかった待ちは失敗としてコールバックへ知らせる。コールバックの中から次の`http_request`を呼んでも待ちの後ろに並ぶ。
   - **`pico.json_decode(text [, keep_null])` / `pico.json_encode(v)` / `pico.json_null`**(`src/lua/LuaJson.hpp`、ヘッダのみ=ホストテストのビルド一覧を増やさないため)。
     Decodeは再帰下降でLuaの値を直接作る(深さ16・入力64KiB、整数は整数、`null`は既定でnil)。Encodeは確保しない関数だけで読み`std::string`へ出す(longjmpしない)。
     空テーブルは`[]`、穴のある配列は`null`で埋める、まばらな整数キー/文字列以外のキー/NaN/循環は失敗(`nil, 理由`)。
     C++側の`Json_Reader`(SAX、値256B・キー48Bの上限)は使わなかった(汎用の変換には小さすぎる)。
5. **タイマー**: `pico.after(ms, fn)`/`pico.every(ms, fn)`/`pico.cancel(handle)`。固定16個(`kMaxTimers`)、ハンドルは`(世代<<8 | 添字+1)`で、解放済みハンドルが別のタイマーを巻き込まない。
   `LuaScene::onUpdate()`が毎フレーム`UpdateTimers(dt)`(`setup()/loop()`の有無に関わらず)。`every`は遅れても溜めずに1回(次は「今からms後」)。
   コールバックのエラーはそのタイマーを止める。別の画面へ`push_scene`している間は動かない(シーンごと`LuaEngine`が作り直されるため)。

- 検証: `lua_engine_test`(JSON・タイマー・タッチ引数・受け渡し・`require`・**127.0.0.1に立てた小さなサーバ(スレッド)相手の実HTTP**: ヘッダ送信・応答ヘッダ・順番待ち・POST・`save_to`で40000バイト・取り消し・権限)、
  `lua_scene_test`(実際のシーン遷移で`args`→`pop(result)`→`on_result`、`on_suspend`→`on_resume`)、`lua_sandbox_test`(`require`の扱いを更新)。
  `lua_engine_test`は`-pthread`でビルドする。環境変数`LUA_TEST_VERBOSE=1`でテストのログ(Luaのエラー等)が標準エラーへ出る。PCビルドでは「ウィジェットAPI確認」アプリで
  盤面のタップ→マス目、タイマー、`push_scene`→`pop(result)`、`store_save`を`--tap`/`--shot`で確認した。
- **既知の限界/未確認**: 実機でのスタック(`ScanRequires`と先読みのコンパイルが想定どおり浅いか)・`save_to`の速度(SDとTLS)・`http_request`の戻り値の変更で`== true`と比べていた既存アプリが無いこと
  (リポジトリ内のLuaアプリは`http_request`を使っていない)。OOM時に`PendingHttp`/`std::string`が漏れうる(`luaL_error`のlongjmpはデストラクタを飛ばす。アプリを閉じる状況なので許容)。
- 洗い出したうえで見送った項目は、下の「Lua APIの追加(2026-10-05 その2)」で入れた。

## Lua APIの追加(2026-10-05 その2): 見送ったウィジェット・○項目・暗号化

前節の洗い出しで「見送った」としたものと、洗い出しの○(あると明らかに楽)の項目を入れた。ドキュメントは `lua-api-doc/content/`
(`api/crypto.md` `api/stdlib.md` を新設、`api/widgets` `dialogs` `drawing` `images` `misc` `scenes` `canvas` と `reference/*` `guide/events.md` を更新)、
動作確認は「動作確認」アプリの「追加の部品」(`pico-os-apps/apps/api-test/app/extra.lua`)。**実機では未確認**(PCのホストテストとPCビルドのみ)。

**ソースの置き場**: `LuaEngine.cpp` が4400行を超えたので、追加分は `src/lua/LuaEngine_Ext.cpp`(ウィジェット補助・イベント・ジェスチャー・リスト/タブ・ダイアログ・
描画の補助・ユーティリティ)と `LuaEngine_Crypto.cpp`(暗号API)に置いた。`LuaEngine` の private へは `friend struct LuaEngineExt` / `LuaEngineCrypto` 経由で触る。
登録は `registerApi()` の末尾の `RegisterExtApi()`(同名は後勝ちなので `list_add` は差し替え)。**ホストテストのリンクの一覧にこの2本と `crypto/Secret_Aead.cpp` と
`lib/monocypher`(`lua_obj/monocypher.o`)を足した**(`LuaEngine.cpp` をリンクする4つの塊すべて)。

**2026-10-19に分け直した**: `LuaEngine.cpp`(約5000行)は実行の土台(予算・サンドボックス・安全網・登録・コールバックの中継・タイマー・画面の受け渡し)だけになり、
`pico.*` の本体は分野ごとの `LuaEngine_{Widgets,System,Sound,Draw,Sd,Http,Require}.cpp`(クラスのメンバのまま。共有の小さな道具は `LuaEngine_Internal.hpp`)。
**拡張(`_Ext`/`_Net`/`_Crypto`/`_Iso`/`_Mic`)は `friend` をやめ**、`LuaEngine.hpp` の「拡張が使う口」(`images()`・`appDir()`・`SdPathAllowed/SdWriteAllowed`・
`markPaletteUsed()`・`UnbindEvent()`・`WireDialogClosed()` 等)だけを通す。拡張ごとの状態は `ExtState`(名前・タブの連動・戻る・ジェスチャー・image_target)/
`IsoState`/`MicState` で、中身はその `.cpp` だけが知る。画像の置き場は `LuaImages`(`lua/LuaImages.hpp`)。新しい拡張で `LuaEngine` の中身が要るときは、
`friend` を足さずにこの口へ1つ足すこと。

1. **Luaから作れるウィジェットを7種足した**(合計27種): `ProgressBar`(新設、タップ素通り)・`TextView`・`ImageView`・`MarkdownView`・`AnalogClock`・`DurationPicker`・`MonthGrid`。
   `WidgetFactory`/`WidgetProperty` の表に足した(新しい `Id`: `ScrollX/Y` `MaxScrollX/Y` `ImageW/H` `RowCount` `Hour/Minute/Second` `HandColor` `SecondHandColor` `TotalMs`
   `Editable` `Year/Month/Today/Selected` `HitTransparent` `Enabled`)。足りなかった口を元のウィジェットへ足した: `TextView::setOwnedText()`(Lua用に文書を自分で持つ。16KiB・確保は更新のときだけ)・
   `setW()`/`setScrollY()`、`MonthGrid::setW/H`、`ImageView::setSize()`(窓を取り直す)、`MarkdownView::loadText()`/`setSize()`(組み直す)。
   - 255バイトを超える文章は `pico.set(id,"text")` に入らない(`Value` の文字列が255B)ので、`pico.text_set(id, text)` を用意した(TextView 16KiB / MarkdownView 8KiB)。
   - **`MarkdownView` は約40KB**使うので1アプリに1つまで(生成を数えて断ってはいない。ドキュメントで注意)。
   - **`Image`/`ImageView`/`MarkdownView` の `path` は `sd_outside_app_dir` の権限で縛った**(`l_set`)。これまで `Image` の `path` が権限を迂回できる穴だった。
2. **新しいイベント**: `duration_changed`(DurationPicker)・`day_selected`(MonthGrid)・`link_tap`(MarkdownView)・`text_tap`(TextView)・`text_input`(Textbox。入力中も `text` が最新)・`scrolled`(ScrollContainer)。
   引数付きのイベントは `BeginDispatch(id, kind)` → 値を積む → `EndDispatch(n)` の口で呼ぶ。`pico.off(id, event)` で解除。
   **ジェスチャー**(`long_press` / `double_tap` / `swipe`)はウィジェットへ配線せず、`LuaScene::onUpdate()` が毎フレーム呼ぶ `LuaEngine::UpdateGestures()` が
   `OSData` のタッチの状態から判定する(`press_*` が同じウィジェットで使われていても干渉しない。登録が1つも無ければ何もしない)。しきい値: 長押し500ms・
   タップ400ms以内・ダブルタップは400ms/24px・スワイプは24px以上を700ms以内。発火先は押し始めの位置を矩形に含む、表示中で有効なウィジェットだけ。
   ホストテストのために `script/host_test/stubs/Arduino.h` の `millis()` を `PicoHostClock::now` で進められるようにした(既定は0のまま)。
3. **ウィジェット操作**: `enabled`(`Widget` 基底に追加。`WidgetFunctions::UpdateAll()` が無効のウィジェットへのタップを受け止めて何も起こさない。`Button` は文字が灰色)・`hit_transparent`・
   `pico.set_name/find/parent/children/get_rect`・`bring_to_front/send_to_back`(`WidgetFunctions::BringToFrontTree/SendToBackTree`。部分木ごと動かす。コンテナの子には使えない)・
   `scroll_to`・`show_keyboard/hide_keyboard`(`causeOnPressStart()` を呼ぶだけ)。
   リストは `list_insert/remove/get/select/scroll_to` と `list_add` のオプション(`icon`/`color`)(`ScrollList` に `insertAt/removeAt/scrollToIndex`、`DropdownMenu` に `removeAt`)。**インデックスは0始まり**(`selected_index` と同じ)。
   タブは `kMaxTabs` を4→8にして `tab_label/set_label/remove/clear`、**`pico.tab_link(tab, index, widget)`**(タブが選ばれている間だけ表示。`TabBar::setOnChanged` を `OnTabChanged()` に差し、`pico.on(tab,"tab_changed")` の有無に関わらず動く)。
4. **ダイアログ**: `closed` の3番目の引数に結果(`DispatchClosed()` が閉じる前に読む)。新設の `PickerDialog`(`dialogs/PickerDialog.hpp/.cpp`。Choice/Date/Time/Number/Progress の5モードを1クラスで)を
   `pico.show_choice/show_date/show_time/show_number/show_progress` で出す。Choiceは1タップで選んで閉じる。Dateは `MonthGrid`+`[<][>]`。Timeは `DurationPicker`。Numberは `NumberInput`。
   Progressは自動では閉じず、`pico.set(id,"value",0〜100)` で進めて `pico.destroy` で閉じる。`WidgetType::PickerDialog` を足した(Countは64未満のまま)。
5. **描画**: `draw_text` の第6引数 `align`・`draw_text_wrapped`・`measure_text`(`WrapLineLength` が空白と文字で折る)・`get_pixel`・`canvas_get_pixel`。
   **オフスクリーン画像**: `pico.image_create(w,h[,transparent])` / `image_target(handle|nil)` / `image_clear`。`image_target` は `OSData::frame` を画像のスプライトへ差し替え、
   `LuaOffscreen::active` の間は `pico.draw_*` が画面のdirtyを積まない(`LuaMarkDirty()`)。**`ProtectedCall()` の一番外を抜けると必ず画面へ戻す**(`EndImageTarget()`。戻し忘れで他の描画が吸い込まれない)。
   画像スロットは4→8枚・64→96KiBに増やした。
6. **ユーティリティ**: `app_dir/path_join/time/wifi_status/url_encode/url_decode/base64_encode/base64_decode/settings_get/set/all/memory_info/toast`、`pico.on_back(fn)`/`pico.go_back()`
   (`LuaScene::onKey()` がEscを、`onUpdate()` がコントローラーのHOMEを「戻る」として渡す。登録が無ければ何もしない)。`util/Base64.hpp` を新設。
   `settings_*` はアプリのフォルダの `settings.cfg`(`PICO_Config`)。`toast` は `NotificationFunctions::Post()` の音なし(履歴にも残る。300msの間隔を空ける)。
7. **OS同梱のLuaモジュール**(ソースは`src/lua/builtin/*.lua`、`script/codegen/gen_lua_builtins.py`が`src/lua/LuaBuiltinModules.hpp`(生成物、コミットする)へ埋め込む。直したら生成し直すこと(CIが`--check`で見る)。フラッシュにはコメント・字下げを落として置く(改行は残すので行番号はそのまま。`performance.md`「フラッシュを削る」)。ソースはフラッシュに置くだけで、`require` されたときだけコンパイルする): `pico.ui`(宣言的なUIの組み立て)・
   `pico.async`(`async.run/await/sleep/http/message/input/choice/date/time/number/...` でコルーチン連携)・`pico.tween`。`preloadModules()` が `pico.` で始まる名前を
   アプリのフォルダより先に同梱ソースから読む(実行中のコンパイルを避ける「先読み」の仕組みに乗せた)。**静的な `require("pico.ui")` の書き方だけ**先読みされる。
8. **暗号化**(`crypto/Secret_Aead.hpp/.cpp`、`LuaEngine_Crypto.cpp`): `pico.encrypt(plain[, password])` / `decrypt` / `is_encrypted` / `hash`(BLAKE2b-256・鍵付き可)/ `random_bytes`、
   `pico.store_save(tbl, {encrypt=, password=})` / `pico.store_load({password=})`。**XChaCha20-Poly1305(Monocypher)+パスワードならArgon2id(64KiB・3パス)**。形式は
   `"enc2:"`+base64url(`version(1)`[+`passes(1)`+`blocks(2)`+`salt(16)`]+`nonce(24)`+暗号文+`MAC(16)`)で、ヘッダもAADに入れて認証する。
   - **パスワード無し**は鍵が「`PICO_Secret::kKey`(ファームに焼かれた固定鍵)+アプリのフォルダ名」なので、守れるのは**SDだけを盗まれる場合と、別のアプリが復号すること**だけ
     (`Secret_Cipher.hpp` と同じ限界)。**パスワード付き**はファームを吸い出されても読めない(強いパスフレーズなら)。どちらも実行中のメモリを覗く相手と暗号文の巻き戻しは防げない。
   - 復号側は壊れた/悪意のあるヘッダで大量のメモリを使わないよう、Argon2のブロック数(8〜256)とパス数(1〜10)に上限を設けてある。平文は12KiBまで。
   - Argon2の作業領域64KiBは**1KiBのブロックごとに `malloc`** して使い終わったら消す(Luaの予算の外。`crypto/Argon2_Split.hpp`)。実機は2026-10-11の計測で暗号化/復号とも約50〜56ms。
     - **以前は `crypto_argon2()` へ連続した64KiBを1つ渡していて、実機で「時々失敗し、一度失敗すると再起動まで失敗し続ける」不具合になった(2026-10-21)**。空きヒープが全部で80〜100KBしかないところから連続64KiBを毎回取り直していたため、使い終わって空いた穴へ長生きする確保が1つ入るだけで連続64KiBが二度と取れなくなり(アプリを開き直しても戻らない)、「メモリが足りません」で失敗し続けた。計算はMonocypher 4.0.2の `crypto_argon2()` を写してブロックの引き方だけ変えたもので、結果はビット単位で同じ(`argon2_split_test` で突き合わせ。既存の暗号文もそのまま読める)。1回の確保は1KiB(+ブロックを指す表)なので、穴が散らばっていても合計64KiB空いていれば通る。ついでにスタックも約2KiB減らした(最初のブロックの元と最後のブロックの直列化をブロック自身の上で行う)。実機では未確認(Todoist参照)。
   - 乱数は `SecretAead::Random()`(実機は `rp2040.hwrand32()`、PCは `/dev/urandom`。`SshUtil::Random()` と同じ中身を、SSHに依存しないよう複製した)。
9. **開発体験**: `script/host_test/lua/pico_mock.lua`(Luaアプリのテスト用の `pico.*` の偽物。アプリ作者向けには `picoos lua-test` が包む(devtools.md「Luaアプリのホットリロードとテストランナー」)。ウィジェット・イベント・タイマー・ダイアログ・HTTP・SD・保存・画面遷移を記録し、
   `mock.fire/advance/close_dialog/respond` で動かす。`pico_mock_test.lua` が自身のテスト)。`pico.memory_info()` で予算の残りが見える。

**検証**: `lua_ext_test`(新設のグループ `lua-ext`。新ウィジェットのプロパティ・イベント(MonthGrid/TextViewは実際のタップ)・`pico.off`・無効のウィジェットがタップを受けないこと(`UpdateAll()`)・
ジェスチャー(スワイプ/長押し/ダブルタップ/遅い動き/矩形の外)・スクロール・リスト/タブ/連動・5種のダイアログと `closed` の3番目の引数・オフスクリーン画像(画面のdirtyを積まないこと、抜けると戻ること)・
描画の補助・ユーティリティ・`on_back`・暗号(往復・改ざん・パスワード違い・別のアプリ・12KiB・BLAKE2bの既知の値・store)・`pico.ui`/`pico.async`/`pico.tween`)、`widget_factory_test`/`widget_property_test`、
`lua_engine_test`(タブ8個・画像8枚に更新)、`pico_mock_test`。ASan/UBSanで通る。
