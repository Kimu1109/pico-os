# Lua API の設計記録(pico.*)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## Love2D比較で足した拡張API (2026-10-04)

Love2Dにあってpico-osのLua APIに無かったもののうち、C++側にほぼ実装があり橋渡しだけで済むものをまとめて足した。
ドキュメントは`lua-api-doc/content/api/`(drawing/images/sound/sdcard/misc)。**ハード制約で難しいもの
(半透明・Shader・Mesh・物理エンジン・スレッド・マルチタッチ)と、変換/座標変換のスタック(`translate/rotate/scale/push/pop`)・
`require`・OGG/MP3・複数WAVの同時再生・Quad/SpriteBatch・フォント差し替えは対象外のまま**。

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
- 動作確認は「動作確認」アプリの「直接描画」(`pc/sdcard/lua/apps/動作確認/draw.lua`。図形・太線・画像の拡大/回転/反転・扇形・右寄せ文字・`on_key`・電池を1画面に描く。PCビルドの`--tap`+標準入力の`key`行で確認済み)。
- 検証: `lua_engine_test`(run.sh。dirty矩形・引数エラー・塗りのピクセル(正方形/L字/扇形)・画像の等倍/拡大/反転/回転/原点/透過・
  文字幅・WAV・battery・sd_stat/sd_read_part・on_keyの登録/解除/エラー)。PCビルドは通る。**実機・PCビルドでの見た目は未確認**
  (`fill_polygon`の1画素ごとの`drawFastHLine`、`draw_image_ex`の全画素ループの速さは実機で見ること。クリップが全面だと最大240x320回)。

## 直接描画(2026-09-20実装、設計を1度やり直した)

**最初の実装(`pico.draw_*`をloop()/コールバックから素で呼ぶだけ)は動かなかった。**
PCビルドの`--shot`で実際に確認したところ、`pico.fill_rect()`で塗った矩形が
跡形もなく消えた。原因は`PICO_GFX::FlushDirty()`(`src/gui/core/GFX_Functions.cpp`)の
合成方式: dirty矩形ごとに「それを覆うウィジェットが無ければ背景色で塗りつぶし
(`clear_bg`)、そこに重なるウィジェットだけを`renderForce()`で再描画する」。
つまり`OSData::frame`はウィジェットが毎回描き直す前提の合成先であって、単純な
永続キャンバスではない。ウィジェットに属さない場所への直接描画は、次にその領域が
dirtyになった瞬間(シーン遷移時の全画面dirty化を含め、ほぼ必ず起きる)に消え、
誰も描き直さないので二度と戻らない。しかも自分自身の`MarkDirty()`呼び出しが
その場でこの消去を引き起こすため、「1フレームだけ映って消える」のではなく
**最初から一切映らない**。

**正しい実装: `LuaCanvas`ウィジェット(`src/gui/widgets/LuaCanvas.hpp/.cpp`)。**
中身を持たない最小限のウィジェットで、`render()`はLua側が`pico.on(id,"render",fn)`で
登録したコールバックを呼ぶだけ。`render()`は`FlushDirty()`の合成サイクルの**中**
(`clear_bg`の後、`pushSprite`の前)で呼ばれるので、そこで`pico.draw_*`を使えば
正しく合成に参加できる(`CanvasRaster`が自前スプライト+`render()`内`pushSprite`で
同じ問題を解決しているのと同じ理屈。こちらは私有スプライトを持たず直接
`OSData::frame`へ描く分だけ軽い)。`RenderMode::OPAQUE`にしてあるので、
`FlushDirty()`が`render()`を呼ぶ前に自分の矩形を`background_color`で塗りつぶして
くれる(前景だけ描けばよい)。

- **クラス名は`Canvas`ではなく`LuaCanvas`**(`CanvasRaster.hpp`が既に
  `namespace Canvas`(`Canvas::Mode`)を使っており衝突するため)。Lua側からは
  `WidgetFactory::TypeFromName()`で`"Canvas"`として見せている
  (`pico.create("Canvas")`)。`WidgetType`の一覧・`WidgetFactory`(`Create`/`IsCreatable`/
  `TypeFromName`)・`WidgetProperty::Set()`(`w`/`h`)の3箇所に登録してある
  (他の汎用ウィジェットと同じ追加パターン)。
- **`pico.on(id, "render", fn)`は`Canvas`にしか登録できない**(`l_on()`が
  `w->getWidgetType() != WidgetType::LuaCanvas`なら`pico.set`と同様エラーにする)。
  コールバックの配線自体は既存の`press_start`等と同じ「`LuaEngine*`+`WidgetId`だけ
  キャプチャ」方式(`EventKind::Render`を追加しただけ)。
- **再描画のリクエストは2段構え**:
  - `pico.invalidate(id)` — そのウィジェットの`needsRender()`を呼ぶ(画面矩形を
    まるごとdirty化)。`Canvas`に限らず任意のウィジェットに使える汎用API。
  - `pico.mark_dirty(x,y,w,h)` — `PICO_GFX::MarkDirty()`の生の下請け。`Canvas`の
    一部だけ再描画したい等、細かい制御が要る場合向けの低レベルAPI。
  静的な内容は**生成直後のウィジェットが初期状態でdirty**なので、追加の
  呼び出し無しで次の`FlushDirty()`に自動で1回`render()`が呼ばれる。アニメーション等
  毎フレーム描き直したい場合だけ`loop(dt)`から`pico.invalidate()`すればよい
  (「変化が無ければ再描画しない」という他ウィジェットと同じ省エネ方針)。
- **座標は`pico.content_rect()`と同じ絶対スクリーン座標、色は既存プロパティ
  (`border_color`等)と同じPICO 4bitパレット番号(0〜15)**。範囲チェックはせず
  `int8_t`へキャストするだけ(`WidgetProperty::Set()`の色プロパティと同じ割り切り)。
- `draw_text`だけは幅が要る(dirty矩形の計算とはみ出し防止のため)。専用の幅計算APIを
  足す代わりに、**残りスクリーン幅(`SCREEN_WIDTH - x`)へ自動で収める**簡易な実装にした
  (`AppGrid::drawName()`のmaxWidthクリップと同じ考え方)。
- **`pico.draw_*`系が呼ぶ`MarkDirty()`は、`render`コールバック内では実質no-op**
  (`FlushDirty()`が合成中`isDirtyDeactivates=true`にしているため)。無害だが
  意味も無いので、`Canvas`の外(loop()等)から`pico.draw_*`を単独で呼んでも
  表示は持続しない点は変わらない——**`pico.draw_*`は必ず`render`コールバックの
  中で使うこと**。
- ホストテストは`lua_engine_test.cpp`。`OSData::frame`はホストテスト用スタブ
  (実際には描画しないダミー実装)で、`canvas->renderForce()`を直接呼んで
  `FlushDirty()`の呼び出しを模している。確認できるのは「クラッシュしないこと」
  「`render`コールバックが実際に呼ばれること」「`pico.invalidate`/`mark_dirty`が
  期待通りの矩形を`PICO_GFX::MarkDirty()`へ渡すこと」まで(見た目の確認は
  PCビルドの`--shot`で行った。`pc/sdcard/lua/hello.lua`に`Canvas`で顔アイコンを
  描く実例がある)。

## 直接描画エリア(2026-09-20実装)

`pico.set_draw_area(x,y,w,h)`/`pico.clear_draw_area()`は`OSData::frame`の
クリップ矩形(`setClipRect`/`clearClipRect`、`Label`/`ScrollList`/`MarkdownView`の
表描画等が既に使っている仕組み)をそのままLuaへ橋渡ししただけの薄いラッパー。
用途は「`Canvas`の`render`コールバック内で、ウィジェット自身の矩形からはみ出す
描画を防ぐ」こと(例: 円グラフの角度計算やスクロールする描画内容が、意図せず
`Canvas`の外まで塗ってしまうのを防ぐガード)。

**`OSData::frame`は全ウィジェット共有の1枚のスプライトで、クリップ矩形も1個しか
持たない。** そのため`set_draw_area()`を呼んだままLua側の`render`コールバックを
抜けると、次にそのクリップ矩形が有効なまま他のウィジェットの`render()`が呼ばれ、
**そのCanvas以外の描画まで巻き込んで切り詰められてしまう**(1フレームだけでなく
`clearClipRect()`されるまでずっと)。これを防ぐため、`LuaCanvas::render()`が
`on_render()`(=Luaのrenderコールバック)から戻った直後に無条件で`clearClipRect()`
する安全弁を入れてある(`LuaCanvas.cpp`)。スクリプト側が`clear_draw_area()`を
呼び忘れても、「そのフレーム内で他のウィジェットまで巻き込む」事故だけは防げる
(呼び忘れたスクリプト自身の後続描画がその場で変な形に切り詰まることまでは
面倒を見ない——そこはスクリプトの責任)。

## SDカードアクセス(2026-09-20実装)

`pico.sd_exists/read/write/remove/mkdir/list`は`SD_Functions`/`FileExplorer`/
`PICO_IO`が既に持っている操作をLuaへ薄く橋渡ししただけで、新しい判断はほぼ無い。

- **`OSData::SD_usable == false`の間はどれも例外にせず失敗値(`false`/`nil`)を返す**。
  SD無しは配線の状態であってスクリプトの書き方の誤りではないため、
  `pico.create`の未知種別のような「プログラマの誤り」枠(`luaL_error`)には入れなかった。
- **`pico.sd_read`には上限(`kMaxSdReadBytes` = 16KiB、`LuaScene::kMaxScriptBytes`と
  同じ値)がある。** スクリプト読み込み(`LuaScene`)は上限超過時に「切り詰めて使う」
  判断をしているが、任意のデータファイルを同じように黙って切り詰めると、
  スクリプトが壊れたJSON/セーブデータを気付かずに使ってしまう恐れがあるため、
  **`sd_read`は切り詰めずに`nil`を返して失敗させる**設計にした(スクリプト読み込みとは
  意図的に判断を変えた点)。
  読み込み自体は`MarkdownView::load()`/`LuaScene::loadAndRun()`と同じく、
  ファイル全体ぶんの一時バッファをヒープへ一度に確保せず、256Bのスタックチャンクで
  `luaL_Buffer`へ読み進める(Lua文字列自体はLua側の確保になるので、
  `LuaEngine`のメモリ予算(既定200KB)がそのまま上限としても効く)。
- **`sd_remove`はディレクトリと判明したら`PICO_IO::removeRecursive()`を使う**
  (`FileExplorer::on_press_delete()`と同じ判断)。
- **`sd_list`は`{name=..., is_dir=...}`の配列を返す**(`FileExplorer::update_list()`と
  同じ`openNext()`の走査。ファイル名バッファも同じ128B)。
  ディレクトリという概念を持たないホストテストのSdFatスタブでは空配列しか返らない
  ため、`lua_engine_test.cpp`では「クラッシュしないこと」までしか確認できておらず、
  実際の列挙結果はPCビルド(`pc/compat/SdFat.hは実ファイルシステム`)の`--shot`で
  確認した(`sd_write`→`sd_read`→`sd_exists`→追記→`sd_list`→`sd_remove`→
  再度`sd_exists`の一連が期待通りに動くことを確認済み)。
- **`sd_read`/`sd_write`等はFsFileを開いたままLuaのC API(`luaL_Buffer`/テーブル構築)を
  呼ぶ。** その最中にLua側のメモリ予算超過でエラー(`longjmp`。ANSI Cのsetjmp/longjmp
  ベースなのでC++デストラクタは呼ばれない)が起きると、開いたままの`FsFile`の
  `close()`が飛ばされ得る。ごく小さな読み書きの最中に限られる稀なエッジケースであり、
  「Lua着手前の受け皿の状態」表にある**OS内部90箇所のOOM未対応と同じ割り切りで
  対象外**とした(そこまで手を入れる投資対効果は低いと判断)。

## 設定ファイルとapp.cfgの書き込み制限(2026-09-30実装)

`pico.config_read/config_get/config_write`。セーブデータや設定を`pico.sd_read`+自前の文字列解析で扱っていたのを、
OSと同じ`key=value`書式(`Config_Functions`)で読み書きできるようにした。読みは`PICO_Config::ParseFile()`、
書きは`PICO_Config::SetValue()`(一時ファイル経由で1キーだけ差し替え)をそのまま使う。

- 値は常に**文字列**で返す(型の解釈はスクリプト側)。書くときは文字列/数値/真偽値を受け、`AsInt/AsFloat/AsBool`で
  読み戻せる表記にする(小数は`%.6f`から末尾の0を削る。`AsFloat()`が指数表記を拒むので`%g`は使えない)。
- **改行を含む値は`false`**(次の行として別のキー=権限等を差し込めるため)。不正なキー(`=`・改行・前後の空白・`#`始まり)は`luaL_error`。
- 読みは`pico.sd_read`と同じ16KiB上限。ファイルが無い/ディレクトリならnil。

**app.cfgの書き込み制限**: `app.cfg`は`permission_network`/`permission_sd_outside_app_dir`を持つので、
スクリプトが書き換えられると自分の権限を上げられてしまう(次回起動のスキャンで効く)。書き込み系の共通ガード
`LuaEngine::SdWriteAllowed()`(`sd_write`/`sd_remove`/`sd_mkdir`/`canvas_save`/`config_write`が通る)で、
権限に関わらず以下を拒否する(`false`+`LOG_APP_WARN`):

- 自分の`app_dir`直下の`app.cfg`、および`/lua/apps/<名前>/app.cfg`(どのアプリのものでも)。後者は
  `sd_outside_app_dir`を持つアプリが他のアプリの権限を上げる・新しいアプリを`app.cfg`付きで作るのを防ぐため。
- `sd_remove`では、それらを含むディレクトリ(自分の`app_dir`とその祖先、`/lua/apps/<名前>`とその祖先)も。
- パスは`normalize()`の後、**FATと同じく大小を区別せず、各セグメントの先頭の空白・末尾の空白と`.`を捨てて**比べる
  (`APP.CFG`や`app.cfg.`も同じファイルを指すため)。
- 読むのは自由(`sd_read`/`config_read`)。
- ホストテストは`lua_engine_test.cpp`(読み書きの往復・後勝ち・型ごとの書式・改行の拒否・各経路でのapp.cfg拒否・
  大文字/末尾の`.`/`..`・`sd_outside_app_dir=true`でも他アプリの`app.cfg`は拒否)。

## 画像(2026-09-21実装)

`pico.image_load/image_size/draw_image/image_free`。ウィジェット(`Image`)を介さず、
`.pimg`(`script/generate_pimg.py`生成の4bpp+RLE独自形式。詳細は上の「MarkdownView実装詳細」
「文書内の参照(画像)」参照)をLuaスクリプトが直接デコードして持ち、描いて、解放できるようにした。

- **デコードは新規実装せず、既存の`IconRender::LoadPimgToSprite()`/`DrawPimgSprite()`
  (`src/gui/icons/icon_render.h/.cpp`)をそのまま呼ぶ。** `Image`ウィジェットの
  `onRAM=true`のとき(`Image::updateSprite()`)と全く同じ経路で、`.pimg`以外の画像形式
  (PNG/BMP等)は最初から対象外(このOSでは`.pimg`だけが唯一の画像形式)。
- **ウィジェットではないので`WidgetRegistry`は使わず、`LuaEngine`インスタンスごとの
  固定長スロット配列(`images_`、上限`kMaxLuaImages=4`)を持つ。** MarkdownViewの
  `labelPool`/`imagePool`と同じ「固定長配列」志向で、任意個数の`std::vector<Widget*>`的な
  管理はしていない。ハンドルは`WidgetId`と同じ発想の
  「generation(上位)+index(下位、1始まり)」パック整数だが、この配列専用のスコープなので
  `WidgetId`のような32bit全体を型ビットまで使う配分は真似ていない。
- **generationは`WidgetRegistry`と同じく、解放(`pico.image_free`)のたびに1つ進める
  (使用中かどうかは別の`ImageSlot::used`フラグで見る)。** 実装時に一度、
  「解放時にgenerationを0(未使用)へ戻し、次のロード時に1から数え直す」という誤った
  設計で書いてしまい、解放直後に同じスロットを再利用すると**前回発行分と全く同じ
  ハンドル値を再発行してしまう**(解放済みの古いハンドルが新しい画像を指してしまう
  use-after-free相当のバグ)ことに気づいて直した。`WidgetRegistry::Unregister()`が
  「`widget=nullptr`にするがgenerationは戻さず進める」設計にしている理由と同じ。
  `script/host_test/lua_engine_test.cpp`に「解放→再利用→古いハンドルがエラーのまま」
  の回帰テストを入れてある。
- **デコード後のピクセルバッファ(`LGFX_Sprite::createSprite()`)は`LuaEngine`の
  `budget_bytes`(Lua自体のアロケータ予算)には乗らない、素のOSヒープ確保**
  (`WidgetFactory::Create()`が作るウィジェット本体と同じ扱い)。そのため画像専用に
  別枠の上限を設けた: 同時に保持できる枚数(`kMaxLuaImages=4`)と合計バイト数
  (`kMaxLuaImageBytes=64KiB`、4bppなので`width*height/2`で概算)の両方。
  超過時は`pico.image_load`が`nil`を返すだけ(`pico.sd_read`等と同じく、SD絡みの
  失敗は`luaL_error`にしない方針を踏襲)。
- **`LGFX_Sprite`は`images_`配列の値メンバなので、`pico.image_free()`を呼び忘れて
  スクリプトが終了しても、`LuaEngine`自体の破棄(`LuaScene::onExit()`)で
  デストラクタが確保分を回収する(リークしない)。** `pico.image_free()`はそれを
  待たず即座に解放したい場合向けの明示API(`pc/sdcard/lua/hello.lua`の「戻る」
  ボタンでの呼び出しがその実例)。
- **`draw_image`は他の`pico.draw_*`と同じ「直接描画」の一種**で、`Canvas`
  (`pico.create("Canvas")`)の`render`コールバック内で使うこと(そうしないと
  次にその領域がdirtyになった瞬間に消える。詳細は「直接描画」参照)。描画後は
  画像サイズぶんを`PICO_GFX::MarkDirty()`する。
- ホストテストは`lua_engine_test.cpp`に追加(SD無し時のnil、正常系のハンドル発行・
  サイズ取得・描画・dirty矩形、存在しないパス/壊れたヘッダのnil、スロット枯渇、
  解放→再利用→古いハンドルの無効化、二重解放の無害化、合計バイト数上限)。
  実際の見た目は`pc/sdcard/lua/hello.lua`に画像描画のデモを追加し、PCビルドの
  `--shot`で`.pimg`(`pc/sdcard/img/hello.pimg`、`examples/img/sample.pimg`と同じ
  48x24の色帯サンプル)が実際に描けることを確認済み。

## ラスタキャンバスの保存/読み込み(CanvasRasterのリサイズ・`pico.canvas_clear/save/load`、2026-09-23実装)

「スクラッチパッド」(黒/青ペン+消しゴムの手書きメモアプリ。`pc/sdcard/lua/apps/スクラッチパッド/main.lua`)を
作る過程で見つかった2つの穴を埋めた。`CanvasRaster`(`pico.create("CanvasRaster")`)は自分専用の
`LGFX_Sprite`を持ち続け、`causeOnPressMove()`がタッチのドラッグをそのまま線として焼き込む
ウィジェットで、手書き入力の土台としては元から使えた。しかし:

1. **`WidgetFactory::Create()`が100×100固定で、`w`/`h`をリサイズする手段が無かった**
   (`WidgetProperty::Set()`のCanvasRaster caseにW/Hが無かった)。全画面に近いスクラッチパッドを
   作ろうとして初めて踏んだ。
2. **クリア・保存・読み込みの手段が無かった**(`CanvasRaster::canvasClear()`自体はC++側に
   元からあったが、Luaへ橋渡ししていなかった。保存/読み込みは影も形も無かった)。

対応:

- **`CanvasRaster::setW/setH`**(内部の`resize(int16_t,int16_t)`)が`LGFX_Sprite::createSprite()`を
  呼び直してサイズを変える。**既存の描画内容は消える**(コンストラクタと同じ手順をやり直す
  ため)ので、生成直後に一度だけ呼ぶ使い方を想定している。`WidgetProperty::Set()`のCanvasRaster
  caseへ`Id::W`/`Id::H`を足しただけで、`pico.set(id,"w"/"h",...)`から使える(`Id::W`/`Id::H`の
  **取得**は元々`GetCommon()`が全ウィジェット共通で処理しているので、Get側の変更は不要だった)。
- **`pico.canvas_clear(id)`**: `CanvasRaster::canvasClear()`を橋渡しするだけ。
- **`pico.canvas_save(id, path)` / `pico.canvas_load(id, path)`**: `CanvasRaster`のスプライトを
  `.pimg`(4bpp+RLE、`script/generate_pimg.py`と同じ形式。上の「画像」参照)としてSDへ書き出す/
  読み込む。**`.pimg`は元々デコード専任(C++側にエンコーダが無かった)**だったが、
  実行中に描いた内容をその場で保存する用途はデコードだけでは足りないため、
  `IconRender::EncodePimg()`(`src/gui/icons/icon_render.h/.cpp`)を新設した。
  行優先(ラスタスキャン)で`sprite.readPixelValue(x,y)`を読みRLE符号化する、
  `generate_pimg.py`のC++版エンコーダ。読み込み側も`LoadPimgToSprite()`(新規に
  スプライトを確保する)とは別に`IconRender::DecodePimgBody()`を新設し、**呼び出し側が
  既にサイズを合わせたスプライトへ直接デコードする**(`pico.canvas_load()`が
  `CanvasRaster`自前のスプライトへそのまま読み込むための版)。
- **`pico.canvas_load()`は読み込んだ`.pimg`のwidth/heightへ`CanvasRaster::resize()`で
  合わせ直す。** 保存時と現在のキャンバスサイズが食い違っていても読み込める
  (=`w`/`h`も画像のサイズへ変わる)。**不正/悪意のあるファイルが画面サイズを超える
  width/heightを名乗っていても、`resize()`を呼ぶ前(=大量確保が起きる前)に
  `SCREEN_WIDTH`/`SCREEN_HEIGHT`基準の上限チェックで弾く**安全策を入れてある。
- 3関数とも対象が`CanvasRaster`以外(または無効なID)だと`luaL_error`
  (`pico.on(render/closed)`と同じ「対応する種別以外はエラー」という約束)。
  `canvas_save`/`canvas_load`のSD絡みの失敗(SD無し・`sd_outside_app_dir`権限無しで
  app_dir外・ファイル不正)は`pico.sd_*`と同じく`false`を返すだけでエラーにはしない。
- **ホストテスト(`lua_engine_test.cpp`)でRLEバイト列の完全一致まで検証するため、
  `script/host_test/stubs/LovyanGFX.h`の`LGFX_Sprite`スタブを「実際にピクセルを
  読み書きする」よう拡張した**(以前は`writePixel`/`clear`が完全な無描画no-opで、
  `readPixelValue`自体が無かった)。他のメソッド(`drawRect`等)は従来どおり無描画のまま。
- ホストテストは`lua_engine_test.cpp`に追加(生成直後100×100・リサイズの往復、
  SD無しの失敗、保存したバイト列が`script/generate_pimg.py`と同じRLE形式になることの
  完全一致比較、クリア後に白へ戻ること、保存→クリア→読み込みの往復、保存時と異なる
  サイズの`.pimg`を読み込むとキャンバス自体がリサイズされること、画面サイズ超過の
  width/heightを拒否すること、対象種別/ID検証、`app_dir`配下への閉じ込め)。
  実際の見た目・操作性はPCビルドの`--shot`(`--tap`でボタンを押しての動作確認)で
  確認した。

## 実機未検証だったCanvasRasterの潜在クラッシュ(`drawWideLine`、2026-09-23発見・修正)

スクラッチパッドを実際にドラッグ操作(`--tap`を連続で細かく打ち、フレーム間で
座標が変わることで`isTouchMove`を発生させる。CLAUDE.md「PC / Web実行環境」の
`--tap`仕様参照)で試して**初めて踏んだクラッシュ**。`CanvasRaster`自体は
Luaバインディング着手より前から存在したコードだが、実際に触れて描画するアプリが
これまで一つも無く(`widget_factory_test.cpp`は生成のみ、`lua_engine_test.cpp`は
`writePixel()`で直接ピクセルを書くだけ)、`causeOnPressMove()`の描画経路が
**本物のLovyanGFXバックエンドに対して一度も実行されたことが無かった**。

- **原因**: `causeOnPressMove()`(Lineモード)と`drawArrow()`(Arrowモードの軸線)が
  `LGFX_Sprite::drawWideLine()`を呼んでいたが、これは内部で`draw_gradient_wedgeline()`
  →`fillRectAlpha()`→`readRect()`という、**アンチエイリアスのため既存ピクセルを
  読み戻すアルファブレンド経路**を通る。`CanvasRaster`のスプライトは4bppの
  パレットモード(`setColorDepth(4)`)で、この読み戻し(`copy_palette_affine`→
  `bgr888_t::get()`)がSEGVした。**AnalogClockの針が`drawWideLine()`を避けて
  `fillTriangle()`で描いている注記(「4bitパレットに無い中間色をアンチエイリアスで
  要求するので使えない」)と全く同じ制約**だが、AnalogClockでは単なる色化けとして
  現れていたのに対し、こちらは実際のクラッシュとして表面化した(用途・入力経路の
  違いによるものと見られ、根本原因は同一)。
- **対処**: `drawWideLine()`を使わず、`CanvasRaster::DrawThickLine()`が
  「両端の`fillCircle()` + 胴体の`fillTriangle()`2枚」で太線を塗る(アルファブレンド無し。
  単純な塗りつぶしのみで`readRect()`を経由しない)。`causeOnPressMove()`と
  `drawArrow()`の両方の呼び出し箇所を差し替えた(後者は`Canvas::Mode::Arrow`の
  軸線部分。矢じり自体は元々`fillTriangle()`でアルファブレンド無しだったので無傷)。
  `Rect`/`Ellipse`モード(`drawRect()`/`drawEllipse()`/`fillEllipse()`)は
  `readRect()`を経由しないため元から安全で、変更していない。
  (2026-09-23追記: 当初この節は「`fillCircle()`を半径の半分間隔で重ね塗りする`drawThickLine()`へ
  置き換えた」と書いていたが、**実際にリポジトリへ入ったコードは`drawWideLine()`のまま**だった。
  下の「ドラッグ描画が重い」の修正で実際に置き換えた)
- **見つけ方**: ホストテスト(ASan)はここを検出できない
  (`script/host_test/stubs/LovyanGFX.h`の`drawWideLine`相当は元から未実装/no-opで、
  実際のLovyanGFXコードパスを一切通らないため)。PCビルドの`--shot`で実際に
  ドラッグを模した`--tap`連打を試して初めて再現できた。**「ホストテストが全部
  通る」ことと「実機/PCビルドで実際に動く」ことは別物**という教訓を、この機能の
  実装で改めて踏んだ形になる。
- 修正後、スクラッチパッドで実際に描画→保存→クリア→読み込みの一連が
  PCビルドの`--shot`で正しく動くことを確認済み。

## CanvasRasterのドラッグ描画が重い(線がカクカク・短い線が引けない、2026-09-23修正)

実機のスクラッチパッドで「線がカクカクで滑らかでなく、ある程度長く引かないと線にならない」と
報告された。原因は描画1回あたりの仕事量で、タッチは`loop()`1周につき1回しか読まないため、
1周が重いほど線は少ない点を結んだ折れ線になる。

- **指が動くたびにキャンバス全体(スクラッチパッドで約234x235px)を作り直していた**:
  `causeOnPressMove()`が`needsRender()`(=画面矩形まるごとdirty)を呼び、さらに`render()`が
  毎回`markdirty(g_rect)`を積み直していたので、**同じ全体矩形が1フレームに2枚**積まれた。
  結果、1回の移動ごとに「キャンバス→`frame`の複製」が3回(`update()`内の`render()`+
  `FlushDirty()`の2枚ぶん)、**液晶への転送(4bpp→RGB565変換+SPI)が画面ほぼ1枚×2回**走っていた。
  → 線分の外接矩形(ブラシ半径ぶん広げる)だけを`MarkDirty()`する(`strokeTo()`)。
  `render()`は移動したときしか`g_rect`を積まない。1回の移動の転送量は数百px程度まで減る。
- **`drawWideLine()`**: アンチエイリアスのため既存ピクセルを`readRect()`で読み戻してアルファ合成する
  重い経路(しかも4bppパレットではPCビルドでSEGVする。上の節参照)。`DrawThickLine()`へ置き換えた。
- **CLEARだった**: `FlushDirty()`が毎回背景の白塗り+下の枠線`Rect`の再描画をしてから上書きしていた。
  スプライトで自分の矩形を隙間なく覆うので`OPAQUE`にした。
- **短い線が消えていた理由**: 触れた瞬間には何も描かず、2px以上動いて初めて線を引いていた。
  さらに**指を離したフレームは`WidgetFunctions::UpdateAll()`が`update()`より先に`causeOnPressEnd()`を
  呼んで`is_pressing`を下ろすため、そのフレームの`causeOnPressMove()`は来ない**(最後の区間が落ちる)。
  → 触れた瞬間に点を打ち、1pxでも動けば繋ぎ、離した瞬間にも最後の区間を繋ぐ。
- ~~`Rect`/`Ellipse`/`Arrow`モードのプレビューは今も移動ごとに全体を`needsRender()`する~~ →
  ペイントで解消(2026-09-24)。「前回+今回のプレビューの外接矩形」だけをdirtyにする(下記「ペイント」参照)。
- ホストテストは`lua_engine_test.cpp`(点・線分・離した瞬間のdirty矩形がキャンバス全体ではなく
  線分の周りだけになること)。見た目はPCビルドの`--tap`連打で確認した。**実機での速度は未計測**
  (`FlushDirty()`が5秒ごとにシリアルへ出す`fps`/`push average`で確かめられる)。

## Buttonのアイコン化(`pico.set(id,"icon_id"/"icon_size",...)`、2026-09-23実装)

スクラッチパッドのツールバーをテキストボタンからアイコンボタンへ差し替えて
描画領域を広げる過程で見つけた穴。`Button::setIcon(IconID, IconSize)`
(文字の代わりにアイコンを描く。`getIconId()`はLua側から既に読めていた)自体は
C++側に元からあったが、**`WidgetProperty::Set()`のButtonケースに`Id::IconId`/
`Id::IconSize`が無く、Luaからは`icon_id`が読み取り専用だった**
(`Id::IconSize`はGetすら無かった)。

- `Button::getIconSize()`を追加(既存の`getIconId()`と対になる読み出し口)。
- `WidgetProperty.cpp`のButtonケースへ`Id::IconId`/`Id::IconSize`のGet/Setを追加。
  Setはどちらも`b->setIcon(id, size)`を呼び直す(もう片方は現在値のまま渡す)だけの
  薄い橋渡しで、C++側に新しいロジックは無い。
- アイコンの色は`text_color`(既存プロパティ)に従う(`Button::drawContent()`が
  `IconRender::DrawIcon()`へ渡す前景色がそのまま`text_color`のため)。ボタンの
  背景色を使った状態表現(ペンの色を背景色そのものにする、等)と組み合わせやすい。
- **アイコンボタンにしても`text`プロパティは無効化されない**(`has_icon`が
  trueの間`drawContent()`がtextを無視して読まないだけ)。`icon_id`を設定した後に
  `text`を読み書きしても実害は無いが、画面には出ない。
- `pico.set(id,"icon_id",N)`のNは`src/gui/icons/icons_data.h`の`IconID`の並び順
  そのまま(明示値なしの連番)。**この並びへ新しいアイコンを追加する際は必ず
  末尾(`script/generate_icons.py`の`ICONS`リストの末尾)へ足すこと** —
  途中へ挿入すると、既存のLuaスクリプトが数値で指定しているアイコンが黙って
  別物にすり替わる(この制約自体は`generate_icons.py`にコメントで明記した。
  今回`Save`/`StackPop`/`StackPush`/`Brush`/`Eraser`の5種が追加されているが、
  この時点ではまだLua側が数値のIconIDに依存していなかったため実害は無かった。
  以降は要注意)。ordinal一覧は`lua-api-doc/content/reference/limits.md`の
  「icon_id(IconID)」に載せてある(スナップショットである旨を明記)。
- ホストテストは`lua_engine_test.cpp`の「細部のプロパティ」ブロックへ追加
  (icon_id/icon_sizeのget/set往復、`setIcon()`経由で`has_icon=true`になること、
  `w`/`h`未指定なら箱の大きさが`icon_size`へ追従すること)。実際の見た目は
  スクラッチパッドのPCビルド`--shot`で確認した。

## シーン制御(2026-09-21実装)

`pico.push_scene/change_scene/launch_app`。それまで`pico.pop()`(`SceneFunctions::Pop()`)
しか無く、Luaスクリプトは「自分を起動した画面へ戻る」以外の画面遷移ができなかった。
C++側の`SceneFunctions::Change/Push/Pop`に相当する3つを揃え、Lua同士の複数画面アプリと、
Luaから既存アプリ(C++製含む)へ飛ぶことの両方をカバーした。

- **`pico.push_scene(path)` / `pico.change_scene(path)`**: `new LuaScene(path)`を
  `SceneFunctions::Push()`/`Change()`へそのまま渡すだけ。Lua側が構築できるScene型は
  `LuaScene`(パス文字列1つのコンストラクタ)だけなので、この2つは**Lua同士の画面遷移**
  (複数画面のLuaアプリを組む)専用になる。`LuaScene(path)`のコンストラクタは
  `FixedString`へパスをコピーするだけで失敗し得ないため、`pico.pop()`と同じく戻り値なし
  (要求を登録するだけで、実際の遷移・エラー表示(ファイル不在等)は次のフレーム境界の
  `LuaScene::onEnter()`まで保留される。呼び出し中の今のLuaEngine自身がその場で
  破棄されることはない)。
- **`pico.launch_app(name)`**: 新設した`AppFunctions::LaunchByName(name)`
  (`App_Functions.hpp/.cpp`、完全一致で登録簿を線形探索して見つかれば`Launch(index)`を
  呼ぶだけの薄いラッパー)経由で、ランチャの登録簿にある**任意のアプリ(C++製含む)**へ
  `Push`する。名前が見つからなければ`false`(呼び出し元がスクリプト側のtypoに気づける
  ようにするための戻り値。他は`AppFunctions::Launch()`をC++コードが直接呼ぶ場合と同じで、
  実際のPush自体が成功するか(スタック上限等)までは見ていない)。
- **`push_scene`は新しいシーンをスタックへ退避するのでPop()で戻れる。`change_scene`は
  スタックを消費せず現在のシーンを置き換えるだけ**(`SceneFunctions::Change`と同じ)。
  そのため`change_scene`で入った画面から`pop()`すると、`change_scene`を呼んだ側の画面を
  飛び越して、**その手前**(スタックの先頭)へ直接戻る。PCビルドの`--shot`で
  「push_scene→change_scene→pop」の一連を実際に確認済み(下記サンプル参照)。
- **LuaSceneはPop()で戻ってきたときスクリプトを最初から実行し直す**(既存の仕様、上の
  「`LuaScene`」参照)。そのため`pico.launch_app()`をスクリプトのトップレベルで
  無条件に呼ぶと、そのシーンへPop()で戻ってくるたびに再度発火してしまう
  (実装時にホストテストでこれを踏んだ。ボタンの`press_start`等のイベント越しに
  呼ぶ分には問題ない。`push_scene`/`change_scene`も同じ理由でボタン経由が無難)。
- ホストテストは`lua_scene_test.cpp`に追加(`lua_engine_test.cpp`ではなくこちら。
  実際にシーン遷移まで起こす必要があるため、既存の`FakeLauncherScene`+
  `SceneFunctions::Setup/Update`の結合テスト環境にそのまま乗せた)。
  push_scene/change_sceneは別のLuaスクリプトへ実際に遷移すること・要求がフレーム境界まで
  保留されること・スタック深さの増減(push_sceneは+1、change_sceneは変化なし)、
  launch_appは登録簿のC++製アプリ(テスト用の`OtherAppScene`)へ実際に遷移すること・
  未登録名は`false`を返すことを確認している。
  実際の見た目はPCビルドの`--shot`で確認した:
  `pc/sdcard/lua/hello.lua`に追加した「サブ画面へ」ボタンから`pc/sdcard/lua/hello_sub.lua`
  (`pico.push_scene()`の飛び先。「電卓を開く」で`pico.launch_app("電卓")`、
  「置き換えへ」で`pico.change_scene()`)→`pc/sdcard/lua/hello_sub2.lua`
  (`pico.change_scene()`の飛び先)という3ファイル構成のデモ一式を作り、
  push_scene→launch_app(電卓が実際に開く)、push_scene→change_scene→pop
  (hello_sub.luaを飛び越してhello.luaへ直接戻る)の両方を確認した。

## ダイアログ(2026-09-21実装)

`pico.show_message/show_input/show_file_save/show_file_select/show_color`。
`MsgDialog`/`InputDialog`/`FileSaveDialog`/`FileSelectDialog`/`ColorDialog`を
Luaスクリプトから表示できるようにした(`SearchDialog`はMarkdownブラウザの検索フロー
専用の状態を前提にしているため対象外)。

- **`WidgetFactory`は経由しない。** 上記5種は`WidgetFactory::Create()`が対応する
  汎用部品15種の対象外で(コンストラクタが型ごとに必須の引数を取り、「位置0,0・
  空文字列」といった無難な既定値では作れない)、代わりに各ダイアログ専用の
  `pico.show_xxx()`を用意した。中身は`new Xxx(...)`→`WidgetFunctions::AddDialog()`
  →`setVisible(true)`→`WireDialogClosed()`という同じ手順で、生成した`WidgetId`を返す。
  `WidgetId`の発行自体(`Widget::getId()`の初回呼び出しで`WidgetRegistry::Register()`)は
  どの`Widget`サブクラスでも汎用に効くため、`WidgetFactory`を経由しなくても
  `pico.on()`/`pico.get()`から安全に参照できる。
- **閉じた通知は共通の`pico.on(id, "closed", function(id, is_ok) ... end)`で受ける。**
  新設した`EventKind::Closed`は既存の`callbacks_`(WidgetId+種別→Lua registry ref)に
  そのまま乗せたが、**実際のC++側コールバック配線(`setOnClosed`/`setOnClose`)は
  生成時点(`WireDialogClosed()`)で済ませてしまう**点が他の4イベントと違う。
  理由: ダイアログはモーダルなので、`pico.on()`を呼び忘れても画面に居座り続けては
  いけない。生成時に必ず配線しておくことで、**`pico.on()`の有無に関わらず閉じたら
  `WidgetFunctions::DestroyLater()`されることを保証**し、`pico.on()`は「あれば
  追加でLuaへも通知する」という上乗せの位置づけにした(`BindCallback()`の
  `EventKind::Closed`ケースは`callbacks_`への登録だけ行い、ウィジェット側の配線はしない)。
  `DispatchClosed(id, is_ok)`は`Dispatch(id, kind)`と別メソッドにしてある
  (`is_ok`を2つ目のLua引数として渡す必要があり、既存の「WidgetId 1引数固定」の
  `Dispatch()`とは呼び出し規約が違うため)。
- **`MsgDialog`/`InputDialog`は`setOnClosed()`、`FileSaveDialog`/`FileSelectDialog`/
  `ColorDialog`は`setOnClose()`と、綴りが割れている**(実装時に気づいた既存コードの
  不統一。直さず`WireDialogClosed()`側の`switch`で吸収した)。
- **`InputDialog`の入力文字列/`FileSaveDialog`・`FileSelectDialog`の選択パス/
  `ColorDialog`の選択色は、`Dispatch`の引数に積まず`WidgetProperty`経由で読む設計**
  にした(`pico.get(id, "text"/"path"/"value")`)。`Dispatch`をダイアログの具象型に
  依存させたくなかったため。`WidgetProperty::Get/Set()`はこれまで
  `WidgetFactory::IsCreatable()`の15種だけが対象だったが、この4種(`MsgDialog`は
  `is_ok`だけで完結するので追加のプロパティ無し)を相乗りさせた。`text`プロパティは
  既存のButton/Label/Textbox等と同じId(`InputDialog::getInput()`/`setInput()`に
  マップ)、`path`は既存のImage用Id(`FileSaveDialog::getSavePath()`/
  `FileSelectDialog::getSelectedPath()`。後者は未選択なら`nullptr`→Lua側は`nil`)、
  `value`は既存のNumberSlider用Id(`ColorDialog::getSelectedColor()`。未選択は`-1`、
  型はNumberSliderと違いInt)を流用しており、新規のプロパティId追加は無し。
- ホストテストは`lua_engine_test.cpp`に追加(ハンドル発行・`dialog_roots`登録・
  `closed`のis_ok往復・`pico.get()`での結果読み出し・`pico.on()`を呼ばなくても
  自動的に破棄されること・`closed`イベントがダイアログ以外だとエラーになること)。
  `FileSaveDialog`/`FileSelectDialog`はホストテストのSdFatスタブがディレクトリの
  実体を持たない(パス→内容のフラットな`map`)ため`getSavePath()`/`getSelectedPath()`の
  中身までは確認できず、生成・キャンセル・自動破棄までに留めた。実際の選択結果と
  見た目はPCビルドの`--shot`で確認済み(`show_message`/`show_input`(初期値の
  プレフィル込み)/`show_color`(実際にスワイプ相当のタップでスウォッチ→OKを押し、
  `pico.get(id,"value")`で選択色が読めることまで)。
  なお、この作業でFileExplorerを初めてホストテストへリンクすることになり、
  `script/host_test/stubs/SdFat.h`に`isDirectory()`(`isDir()`とは別綴りのSdFat API。
  `pc/compat/SdFat.h`には両方あるがスタブは`isDir()`だけだった)が無いことに気づいて追加した。

## ネットワーク(2026-09-21実装)

`pico.http_request(method, url, body, content_type, callback)` / `pico.http_cancel()`。
既存の`Http_Get`(`src/net/Http_Get.hpp`)はMarkdownブラウザのキャッシュ用途に
特化していて汎用のHTTPクライアントとしては使えなかった(GET専用でメソッドが
`sendRequestLine()`にリテラル`"GET "`で埋め込まれている/送信ボディの概念が無い/
`HttpBodyGate`が`statusCode()==200`以外の本文を黙って捨てる/200・304以外を
一律`FAILED`扱いにする)ため、新設の`HttpRequest`(`src/net/Http_Request.hpp/.cpp`)を
別クラスとして用意した。

- **`HttpGet`とコード骨格(接続/送信/受信ループ・タイムアウト・リダイレクト追跡)は
  ほぼ同じにしてある**(実績のある形をそのまま踏襲。差分だけ抜き出す共通基底への
  リファクタは、Markdownブラウザの動いているHTTP経路を壊すリスクの方が大きいと
  判断して見送った)。**違うのは以下3点だけ**:
  1. メソッドが`enum class Method { GET, POST, PUT, PATCH, Delete }`で選べる
  2. 送信ボディ+`Content-Type`を指定でき、`Content-Length`ヘッダを自分で付けて
     ボディを送る(`HttpResponse`が読む*応答*の`Content-Length`とは別物)
  3. **`HttpBodyGate`を挟まない**(`res.reset(sink_)`で直結)ため、
     ステータスコードによらず本文がsinkへ渡る。Task自体の成功/失敗も「応答を
     最後まで読めたか」だけで判定し、ステータスコードの意味(2xx/4xx/5xx)は
     呼び出し側(Luaスクリプト)へ渡す。**Markdownブラウザ側の判断(200/304だけ
     成功、他は本文を捨てる)はそのまま`HttpGet`に残しており、この変更の影響は
     受けない**
- **GETだけリダイレクトを自動で追う。** POST等はボディを送り直すべきか
  (RFCの解釈も一律ではない)を決め打ちせず、3xxが返ってきてもそのまま
  呼び出し側(Luaスクリプト)へ渡す判断にした。
- **`HttpGet`/`HttpRequest`はどちらも`PICO_Task`の全体リストには登録されない。**
  実際の利用者(`DocFetch`/`DocSearch`)は`HttpGet`を値メンバとして持ち、
  自分の`update()`から毎フレーム`http.update()`を呼ぶ設計になっている
  (`NetworkScan`だけが`PICO_Task::Add()`でグローバルリストに乗る例外)。
  同じ流儀で、`LuaEngine`が`HttpRequest`を1本(`http_`、後述の理由で遅延`new`)
  保持し、**`LuaScene::onUpdate()`から毎フレーム`engine->UpdateHttp()`を呼んで
  進める**(`setup()`/`loop()`の定義有無に関わらず無条件に呼ぶ。`CallLoop()`
  とは別の独立した呼び出しにしてあり、`pico.*`のネットワーク進行を
  Arduino風`loop(dt)`の意味論と混ぜていない)。
- **同時に走るリクエストは1本まで(2026-10-05からは2本目以降を最大4本まで順番待ちにする。下記「Lua APIの追加(2026-10-05)」)。** `HttpState::callback_ref`が
  `LUA_NOREF`かどうかで「進行中か」を判定する。進行中に`pico.http_request()`を
  呼んでも`luaL_error`にはせず`false`を返すだけ(SD無し等と同じ「実行時の状態」
  枠)。完了時は**Lua側コールバックを呼ぶ前に`callback_ref`を`LUA_NOREF`へ戻す**
  ことで、コールバック自身の中から次の`pico.http_request()`を呼べるようにしてある
  (チェイン可能)。
- **`http_`(`HttpRequest`+受信バッファ+送信ボディの控え。受信/送信とも
  `kMaxHttpResponseBytes`/`kMaxHttpBodyBytes`=16KiBで頭打ち)は初回の
  `pico.http_request()`呼び出しまで`new`しない。** ネットワークを使わない
  Lua アプリ(大半を占める見込み)に32KiB(16KiB×2)の固定バッファを常時
  負担させたくないための遅延生成(`struct HttpState`はヘッダでは前方宣言のみ)。
- **送信ボディ/Content-Typeは呼び出し時にLuaEngine側の`FixedString`へコピーする。**
  `HttpRequest::begin()`は`const void* body`/`const char* content_type`を
  非所有ポインタ(`IHttpSink*`と同じ約束)で受け取るが、Luaスタック上の一時的な
  文字列をそのまま渡すと、`Connecting`フェーズが次のフレームへ回る(`HttpGet`と
  同じ設計。フレーム時間をならすため)間にポインタが無効になりかねない。
  `HttpState::body_buf`/`content_type_buf`へ一度コピーしてから渡すことで解決した。
- **受信本文はNULを含み得るため、Luaへ渡す際は`lua_pushlstring()`(長さ明示)を使い、
  `lua_pushstring()`(strlen前提)にしていない。**
- ホストテストは`lua_engine_test.cpp`に追加。**実ソケットに一切触れない範囲
  (未知のメソッド/不正なURL/送信ボディの上限超過/同時実行数の上限)での
  早期拒否がすべて`false`(またはpcall経由でエラー)になることだけを確認**しており
  (`HttpGet`と同様、実際の通信を伴う検証は`run.sh`(ASan、ネットワーク無し)の
  対象外。`run_net.sh`と同じ「本物のソケット」区分に属する)、送信ボディの上限
  テストで16KiB超の文字列を作る都合上、それまでの全テストで積み上がった共有
  `engine`の予算と衝突しないよう専用の`LuaEngine`を使っている。
  **実際の通信はPCビルドで、使い捨てのローカルHTTPサーバ(`http.server`)を相手に
  動作確認した**(このリポジトリには含めていない検証用スクリプト): GET成功
  (status=200、本文が読める)、POST(送信ボディ+Content-Typeが実際にサーバへ届き、
  エコーされた応答本文が読める)、**404応答でも本文が読めること**
  (`HttpGet`の`HttpBodyGate`なら本文が捨てられていたはずの経路で、`HttpRequest`が
  正しく本文を渡せていることの確認)の3パターンを確認済み。

## ウィジェット固有イベント(2026-09-21実装)

`pico.on(id, event_name, fn)`の共通4種(press_start/end/move/out)+`render`(Canvas限定)+
`closed`(ダイアログ限定)に加え、一部のウィジェットが元々持っていた専用コールバックも
Luaから使えるようにした:

| イベント名 | 対象ウィジェット | 元のC++コールバック |
|---|---|---|
| `checked_changed` | `Checkbox` | `setOnChangeChecked()` |
| `value_changed` | `NumberSlider` | `setOnValueChanged()` |
| `select_item` | `ScrollList` | `setOnSelectItem()` |
| `tab_changed` | `TabBar` | `setOnChanged()` |
| `dropdown_changed` | `DropdownMenu` | `setOnChanged()`(2026-09-21追加。細部の穴埋め) |
| `text_changed` | `Textbox` | `setOnTextChanged()`(2026-09-21追加。細部の穴埋め) |

- **対応するウィジェット種別以外へ登録しようとすると`"render"`/`"closed"`と同じく
  `luaL_error`になる**(`l_on()`側で`getWidgetType()`を見て弾く)。
- **`checked_changed`/`value_changed`/`tab_changed`の3つは、変わった後の値そのものを
  コールバック引数として渡さず、既存の共通`Dispatch(id, kind)`(idのみ渡す)にそのまま乗せた。**
  Checked/Value/TabSelectedはいずれも`pico.get(id, "checked"/"value"/"tab_selected")`で
  読める永続プロパティ(`WidgetProperty`)なので、Lua側はコールバック内でそれを読めば足りる。
  値の型・個数をイベントごとに変える専用Dispatchを増やすより、既存の「値はpico.getで読む」
  という約束に寄せた方が単純だと判断した。
- **`select_item`だけは例外で、`already_selected`(同じ項目を2回連続でタップしたか。
  `SearchDialog`等の「2回タップで開く」判定に使う値)をコールバックの第2引数として渡す
  専用の`DispatchSelectItem(id, already_selected)`を用意した。** これは`ScrollList::on_selectitem`が
  渡す一時的な値で、`selected_index`のような永続プロパティとして持てないため
  (`DispatchClosed(id, is_ok)`と同じ理由づけ)。選択後のindex自体は"select_item"の中で
  `pico.get(id, "selected_index")`を読めばよい。
- コールバックの配線方式(`this`(`LuaEngine*`)+`WidgetId`だけをキャプチャしてヒープ確保を
  起こさない)は共通4種と同じ(`BindCallback()`参照)。
- ホストテストは`lua_engine_test.cpp`に追加。6イベントそれぞれについて、対応するC++側の
  トリガ(`Checkbox::causeOnPressStart()`でのタップ相当/`NumberSlider::setValue()`/
  `ScrollList::causeOnSelectItem()`/`TabBar::setSelected(index, true)`/
  内部`ScrollList`経由の`DropdownMenu`選択/`Textbox::onHide()`)を直接呼んで
  Luaコールバックが実際に発火すること、`pico.get()`で変更後の値が読めること、
  `select_item`の`already_selected`が引数で渡ること、対応外のウィジェットへの登録が
  エラーになることを確認している。

**追加分(2026-09-21、細部の穴埋め): `dropdown_changed`(DropdownMenu)と
`text_changed`(Textbox)。** どちらも元々「作れるのに変化を知る手段が無い」状態だった:
- `DropdownMenu`は元々選択されたことを外へ知らせる仕組み自体が無く(内部で
  `value`ラベルの表示更新だけを完結させていた)、`DropdownMenu::on_changed`
  (`std::function<void()>`。`TabBar::on_changed`と同じ「引数無し、値は
  `pico.get(id,"selected_index")`で読む」形)を新設して`setOnSelectItem()`の
  ラムダ内(表示更新の直後)から呼ぶようにした。`ScrollList`と違い「同じ項目の
  選び直し」がそのまま2回目の確定として飛ぶことは無い(開き直しにしかならない)ため、
  `already_selected`に相当する概念自体が要らず、共通Dispatch組にそのまま入る。
- `Textbox::on_text_changed`(`std::function<void()>`)は**以前からヘッダに宣言だけ
  あったが、setterも発火する場所も無い死んだメンバだった**(実装時に見つけた)。
  `setOnTextChanged()`を追加し、`onHide()`(オンスクリーンキーボードを閉じて
  入力が確定したタイミング。`onTextChanged()`が「入力途中は背景を更新しない」
  方針なのに合わせ、1文字ごとには発火させない)から呼ぶよう配線した。
  ホストテストは実機のオンスクリーンキーボードを介さず、最小限の`ITextInputWidget`
  実装(`FakeKeyboard`)を使って`Textbox::onHide()`を直接呼ぶことで検証している。

## コンテナからの取り外し / リストへの項目追加(2026-09-21実装、細部の穴埋め)

`pico.remove_child(container_id, child_id)`: `pico.add_child`の逆。`pico.destroy(child)`は
子ごと破棄する経路しか無く、「親から外して別のコンテナへ移す」「一旦フリーにして後で
作り直す」といった用途に使えなかった。

- `LayoutContainer`/`GridContainer`/`ScrollContainer`のみ対応(それ以外は`luaL_error`)。
  指定した`child`が実際にその`container`の子でない場合もエラーにする。
- 手順は`WidgetFunctions::Remove(child)`相当: `container->removeChild(child)`(仮想関数、
  各コンテナのoverrideがそのまま呼ばれる)→フラットリスト(`WidgetFunctions::widgets`)へ
  `Add()`し直す。取り外した子は次フレームから独立したルートウィジェットとして
  描画・当たり判定の対象になる。
- **`Widget::removeChild()`の3つのoverride(`LayoutContainer`/`GridContainer`/
  `ScrollContainer`)全てに`child->setParent(nullptr)`を足した(実装の副産物)。**
  以前は`children_`から外すだけで親ポインタが残ったままになっており、
  `WidgetFunctions::Destroy()`経由(直後に`delete`するので実害が無い)でしか
  呼ばれていなかったため問題が顕在化していなかった。`pico.remove_child`で
  子を**生かしたまま**取り外す経路ができたことで、親ポインタを残すと
  取り外し後も旧コンテナ基準で`getScreenRect()`等を計算してしまうバグになるため、
  必須の修正として一緒に入れた。
- **取り外し後もx/y座標はコンテナ内での相対値のまま残る**(コンテナが管理していたのは
  位置決めだけで、子自身の`l_rect`はコンテナ座標系の値を持ち続ける)。`pico.create()`
  直後と同じく、呼び出し側が`pico.set(id,"x"/"y",...)`で置き直す前提。
- ホストテストは`lua_engine_test.cpp`(既存の`add_child`テストの続きとして追加)。
  取り外し後に親が`nullptr`になること・コンテナの`children_`から外れること・
  破棄されずフラットリストへ独立したルートとして戻ること・`WidgetId`がまだ有効な
  ことを確認している。

`pico.list_add(id, text)` / `pico.list_clear(id)` / `pico.tab_add(id, label)`:
`ScrollList`/`DropdownMenu`/`TabBar`は`pico.create()`で生成できるのに、中身を
増やす手段が無かった(C++側は`ScrollList::add()`/`DropdownMenu::add()`/
`TabBar::addTab()`を直接呼べるが、Luaから叩く経路が無かった)。

- `pico.list_add`はアイコンを指定できず既定(`IconID::AppBox`)固定
  (`ScrollList`は`enable_icon`がfalseの間そもそも描かれない)。名前→`IconID`の
  変換表を足す話は将来の拡張として残し、今回は「文字列を足せる」ことを優先した。
- `pico.list_clear`で`DropdownMenu`を空にする場合、項目を消すだけでなく表示ラベルも
  プレースホルダへ戻す(`DropdownMenu::clear()`新設)。選択済みの表示だけが
  残ってしまわないようにするため。
- `pico.tab_add`は`TabBar::addTab()`の戻り値(`kMaxTabs=4`超過で`false`)をそのまま返す。
  呼び出し側がタブ数の上限に達したことを検知できるよう、`luaL_error`にはしていない。
- 副産物として`ScrollList::getItemCount()`/`DropdownMenu::getItemCount()`を追加し、
  `WidgetProperty`へ`item_count`プロパティ(`pico.get(id,"item_count")`)として
  乗せた(読み取り専用。`list_add`/`list_clear`後の件数確認に使う)。
- ホストテストは`lua_engine_test.cpp`。2種のウィジェットへの追加・全消し・件数確認、
  `TabBar`の上限到達、対応外ウィジェットへの呼び出しがエラーになることを確認している。

## 時刻取得(2026-09-21実装)

`pico.get_time()`。`TimeFunctions::timeinfo`(NTP同期後に妥当な値になる。`Setup()`呼び出し
自体はLuaEngineの責務ではなく、main.cppが起動時に済ませている)をLuaへ橋渡しするだけの
薄いAPI。

- 年/月は`TimeFunctions::year`/`month`(`tm_year`/`tm_mon`から1900年オフセット/0始まり月を
  補正済みの値。`ClocksScene`等と同じものを使う)をそのまま使い、残り(日/時/分/秒/曜日)は
  `struct tm`のフィールドをそのまま渡す。`wday`は`tm_wday`そのまま(0=日曜〜6=土曜)。
- **戻り値は`pico.content_rect()`のような複数戻り値ではなく、フィールド名付きの1個の
  テーブル**(`{year=.., month=.., day=.., hour=.., min=.., sec=.., wday=..}`)にした。
  `pico.sd_list()`が`{name=.., is_dir=..}`の配列を返すのと同じ「複数の名前付き値は
  テーブルで返す」という使い分け(`content_rect`はx/y/w/hの4値のみで意味も自明なため
  複数戻り値のままにしてある)。
- **NTP未同期の場合の値の妥当性はこのAPI側では保証しない**(`ClocksScene`等、既存の
  `TimeFunctions`利用箇所と同じ割り切り。`main.cpp`起動時点、あるいはWi-Fi未接続のままの
  場合は同期前の初期値がそのまま返る)。
- ホストテストは`lua_engine_test.cpp`に追加。`TimeFunctions::timeinfo`/`year`/`month`を
  テストから直接書き換え、`pico.get_time()`が返すテーブルの各フィールドと一致することを
  確認している(`Setup()`/`Update()`はNTP同期や`millis()`に依存するため呼ばず、`struct tm`を
  直接埋める)。

## タップ位置の取得(pico.get_touch、2026-09-21実装)

Luaでオセロアプリ(`pc/sdcard/lua/apps/オセロ/main.lua`)を作った際に踏んだ穴を埋めた。
`pico.on(id, "press_start", fn)`等の共通4イベントはWidgetIdしか渡さない(「コールバックの
引数は多くの場合WidgetIdのみ」という既存の設計方針。イベントごとに引数の型・個数を
変える複雑さを避けるための意図的な判断で、これ自体は変えていない)。そのため「盤面全体を
1枚の`Canvas`にして、押された座標からマス目を逆算する」というC++側の`AppGrid`/
`ColorDialog`と同じパターンがLuaからは組めず、オセロの初版はマスの数(8×8=64個)だけ
小さな`Canvas`を敷き詰めて、それぞれの`press_start`で「どのマスが押されたか」を判定する
遠回りをしていた。

- **`pico.get_touch() -> x, y, is_touched`を追加した。** 中身は`OSData::touchX/touchY/
  isTouched`をそのまま返すだけの薄いAPI(`pico.get_time()`と同じく、既存のグローバル状態を
  橋渡しするだけで新しい状態は持たない)。`WidgetFunctions::HitTest()`
  (`src/gui/core/Widget_Functions.cpp`)が当たり判定に使っているのと同じ値なので、
  `press_start`等のコールバックの中で読めば「そのタップが当たったウィジェットの外側から見た
  絶対スクリーン座標」と一致する(`pico.draw_*`/`pico.content_rect()`と同じ座標系)。
- **戻り値は`pico.get_time()`のようなテーブルではなく、`pico.content_rect()`と同じ複数戻り値**
  (`x, y, is_touched`)にした。名前付きのフィールドが3つだけで意味も自明なため
  (`get_time`がテーブルにした基準「複数の名前付き値」に該当するほど多くない)。
- **`isTouchEnd`(離した瞬間)でも座標はリセットされず最後の値を保持したまま**なので
  (`Touch_Functions.hpp`/`Touch_Functions_PC.hpp`の`Update()`、`if(!touched){ ... }`の枝を
  参照。`touched=false`の間`OSData::touchX/touchY`へは一切書き込まない)、`press_end`の中で
  呼んでも問題なく使える。
- **イベント引数のシグネチャ自体(`Dispatch(id, kind)`)には一切手を入れていない。**
  座標を「その場で起きたイベントの引数」として渡す設計にはせず、「今の状態を問い合わせる
  別関数」にしたことで、既存の8種類のイベント(共通4種+ウィジェット固有4種+render+closed)の
  ディスパッチ経路・コールバック中継の仕組み(`LuaEngine*`+`WidgetId`だけをキャプチャする
  設計、上記「コールバック中継の設計」参照)を一切変更せずに済んだ。
- ホストテストは`lua_engine_test.cpp`。`OSData::touchX/Y/isTouched`を直接書き換えてから
  `Widget::causeOnPressStart()`(実際のタップ相当)を発火させ、`press_start`コールバックの
  中で`pico.get_touch()`が同じ値を読めることを確認している。
- ドキュメント(`lua-api-doc/`)にも[タッチ](../../api/touch/)ページを新設し、
  [イベント](../../guide/events/)と[Canvasと直接描画](../../guide/drawing/)の両方から
  相互参照するようにした。オセロアプリ自体は(既に動いていたため)このAPI追加に合わせた
  書き換えはしていない——マス目ごとに`Canvas`を敷き詰める方式も引き続き有効で、
  マスごとに全く違う描画をしたい場合はむしろこちらのほうが素直なことがある。

## Lua APIの追加(2026-10-05): require・タッチ座標・画面の受け渡し・HTTP/JSON・タイマー

「ウィジェットを使うLuaアプリを書くときに足りない機能」を洗い出した(`pico.*`の不足リスト)うち、実害が出ていた5項目をまとめて入れた。
ドキュメントは`lua-api-doc/content/api/`(`modules.md`/`json.md`/`timers.md`/`scenes.md`/`network.md`)、動作確認は「動作確認」アプリの「画面とデータ」(`pc/sdcard/lua/apps/動作確認/scene.lua`)。
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
動作確認は「動作確認」アプリの「追加の部品」(`pc/sdcard/lua/apps/動作確認/extra.lua`)。**実機では未確認**(PCのホストテストとPCビルドのみ)。

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
7. **OS同梱のLuaモジュール**(ソースは`src/lua/builtin/*.lua`、`script/gen_lua_builtins.py`が`src/lua/LuaBuiltinModules.hpp`(生成物、コミットする)へ埋め込む。直したら生成し直すこと(CIが`--check`で見る)。ソースはフラッシュに置くだけで、`require` されたときだけコンパイルする): `pico.ui`(宣言的なUIの組み立て)・
   `pico.async`(`async.run/await/sleep/http/message/input/choice/date/time/number/...` でコルーチン連携)・`pico.tween`。`preloadModules()` が `pico.` で始まる名前を
   アプリのフォルダより先に同梱ソースから読む(実行中のコンパイルを避ける「先読み」の仕組みに乗せた)。**静的な `require("pico.ui")` の書き方だけ**先読みされる。
8. **暗号化**(`crypto/Secret_Aead.hpp/.cpp`、`LuaEngine_Crypto.cpp`): `pico.encrypt(plain[, password])` / `decrypt` / `is_encrypted` / `hash`(BLAKE2b-256・鍵付き可)/ `random_bytes`、
   `pico.store_save(tbl, {encrypt=, password=})` / `pico.store_load({password=})`。**XChaCha20-Poly1305(Monocypher)+パスワードならArgon2id(64KiB・3パス)**。形式は
   `"enc2:"`+base64url(`version(1)`[+`passes(1)`+`blocks(2)`+`salt(16)`]+`nonce(24)`+暗号文+`MAC(16)`)で、ヘッダもAADに入れて認証する。
   - **パスワード無し**は鍵が「`PICO_Secret::kKey`(ファームに焼かれた固定鍵)+アプリのフォルダ名」なので、守れるのは**SDだけを盗まれる場合と、別のアプリが復号すること**だけ
     (`Secret_Cipher.hpp` と同じ限界)。**パスワード付き**はファームを吸い出されても読めない(強いパスフレーズなら)。どちらも実行中のメモリを覗く相手と暗号文の巻き戻しは防げない。
   - 復号側は壊れた/悪意のあるヘッダで大量のメモリを使わないよう、Argon2のブロック数(8〜256)とパス数(1〜10)に上限を設けてある。平文は12KiBまで。
   - Argon2は64KiBを `malloc` して使い終わったら消す(Luaの予算の外)。**実機での時間とRAMは未計測**(RP2350で数十ms〜の見込み)。
   - 乱数は `SecretAead::Random()`(実機は `rp2040.hwrand32()`、PCは `/dev/urandom`。`SshUtil::Random()` と同じ中身を、SSHに依存しないよう複製した)。
9. **開発体験**: `script/host_test/lua/pico_mock.lua`(Luaアプリのテスト用の `pico.*` の偽物。ウィジェット・イベント・タイマー・ダイアログ・HTTP・SD・保存・画面遷移を記録し、
   `mock.fire/advance/close_dialog/respond` で動かす。`pico_mock_test.lua` が自身のテスト)。`pico.memory_info()` で予算の残りが見える。

**検証**: `lua_ext_test`(新設のグループ `lua-ext`。新ウィジェットのプロパティ・イベント(MonthGrid/TextViewは実際のタップ)・`pico.off`・無効のウィジェットがタップを受けないこと(`UpdateAll()`)・
ジェスチャー(スワイプ/長押し/ダブルタップ/遅い動き/矩形の外)・スクロール・リスト/タブ/連動・5種のダイアログと `closed` の3番目の引数・オフスクリーン画像(画面のdirtyを積まないこと、抜けると戻ること)・
描画の補助・ユーティリティ・`on_back`・暗号(往復・改ざん・パスワード違い・別のアプリ・12KiB・BLAKE2bの既知の値・store)・`pico.ui`/`pico.async`/`pico.tween`)、`widget_factory_test`/`widget_property_test`、
`lua_engine_test`(タブ8個・画像8枚に更新)、`pico_mock_test`。ASan/UBSanで通る。
