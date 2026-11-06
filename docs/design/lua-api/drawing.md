# Lua API の設計記録(pico.*): 直接描画・画像・キャンバス

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `lua-api.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

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
  PCビルドの`--shot`で行った。`sdcard/lua/hello.lua`に`Canvas`で顔アイコンを
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

## 画像(2026-09-21実装)

`pico.image_load/image_size/draw_image/image_free`。ウィジェット(`Image`)を介さず、
`.pimg`(`script/tools/generate_pimg.py`生成の4bpp+RLE独自形式。詳細は上の「MarkdownView実装詳細」
「文書内の参照(画像)」参照)をLuaスクリプトが直接デコードして持ち、描いて、解放できるようにした。

- **デコードは新規実装せず、既存の`IconRender::LoadPimgToSprite()`/`DrawPimgSprite()`
  (`src/gui/icons/icon_render.h/.cpp`)をそのまま呼ぶ。** `Image`ウィジェットの
  `onRAM=true`のとき(`Image::updateSprite()`)と全く同じ経路で、`.pimg`以外の画像形式
  (PNG/BMP等)は最初から対象外(このOSでは`.pimg`だけが唯一の画像形式)。
  **その後 BMP・JPEG、2026-10-28に PNG を足し、今は全形式を `ImageCodec::LoadToSprite()` で読む**
  (`gui.md`「画像の形式とフルカラーの直描き」。オプションは `dither`/`raw`/`scale`/`max_w`/`max_h`/`transparent`)。
  **2026-10-28に `full_color`/`background` を足した**(下の「フルカラーの画像(full_color)」)。
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
  待たず即座に解放したい場合向けの明示API(`sdcard/lua/hello.lua`の「戻る」
  ボタンでの呼び出しがその実例)。
- **`draw_image`は他の`pico.draw_*`と同じ「直接描画」の一種**で、`Canvas`
  (`pico.create("Canvas")`)の`render`コールバック内で使うこと(そうしないと
  次にその領域がdirtyになった瞬間に消える。詳細は「直接描画」参照)。描画後は
  画像サイズぶんを`PICO_GFX::MarkDirty()`する。
- ホストテストは`lua_engine_test.cpp`に追加(SD無し時のnil、正常系のハンドル発行・
  サイズ取得・描画・dirty矩形、存在しないパス/壊れたヘッダのnil、スロット枯渇、
  解放→再利用→古いハンドルの無効化、二重解放の無害化、合計バイト数上限)。
  実際の見た目は`sdcard/lua/hello.lua`に画像描画のデモを追加し、PCビルドの
  `--shot`で`.pimg`(`sdcard/img/hello.pimg`、`examples/img/sample.pimg`と同じ
  48x24の色帯サンプル)が実際に描けることを確認済み。

## ラスタキャンバスの保存/読み込み(CanvasRasterのリサイズ・`pico.canvas_clear/save/load`、2026-09-23実装)

「スクラッチパッド」(黒/青ペン+消しゴムの手書きメモアプリ。`sdcard/lua/apps/スクラッチパッド/main.lua`)を
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
  `.pimg`(4bpp+RLE、`script/tools/generate_pimg.py`と同じ形式。上の「画像」参照)としてSDへ書き出す/
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
  SD無しの失敗、保存したバイト列が`script/tools/generate_pimg.py`と同じRLE形式になることの
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

## フルカラーの画像(full_color) (2026-10-28)

`pico.image_load(path, {full_color = true})` で読んだ JPEG・PNG・8bpp以上のBMPを、Canvas の `render` の中の `pico.draw_image` で
**液晶へフルカラーで直接描く**。仕組みは ImageView と同じ `PICO_GFX::ArmDirectPaint`(`gui.md`「画像の形式とフルカラーの直描き」)で、
写真を16bppで持たずに、frameの転送の後にSDから解きながら `pushImage` する(16色の絵は今までどおり持って frame へ描く=下書き)。

- **覚え書きは `LuaImages::FullColor`(パス・`ImageCodec::Info`・縮め方・最後に描いた位置とCanvas)を `kMaxFullColor=2` 個だけ**。
  パス(256B)を全8スロットに持たせると約2KBになるので分けた。超えたら警告して16色で読む(`nil` にはしない)。
  `image_free`(`LuaImages::release`)・`image_target`/`image_clear`(ファイルの絵と食い違う)・`~LuaImages` で `CancelDirectPaint` して外す。
- **頼むのは「今描き直している所(`PICO_GFX::render_clip`)に画像がかかったとき」だけ**。Spotifyの再生位置の欄のように、画像にかからない所だけを
  1秒ごとに描き直す使い方で、毎回SDから解き直さないため。かかったときは画像の見えている所全体(dirty矩形の外も)を描かせる。
  描き直さなかった所の液晶にはフルカラーが残っている(frameと食い違うが、`RunDirectPaint` が行を「次は必ず送る」にしてある)。
- **どの Canvas の上か**は `LuaCanvas::rendering`(`render()` が Lua の関数を呼ぶ間だけ自分を入れる)で知る。render の外・`image_target` の間
  (`LuaOffscreen::active`)は頼まない。矩形は Canvas の `clippedScreenRect()` で切り、`WidgetFunctions::IsCoveredAbove(canvas, …)` なら頼まない。
- **液晶に直描きできるのは画面に1枚だけ**(`directPaint` が1つ)。ほかの持ち主が描いている(`painted`)・描こうとしている(`pending`)間は奪わない。
  奪うと奪われた側の跡が `MarkDirtyBelow` で frame の絵へ戻され、その描き直しでまた奪い返す…を毎フレーム繰り返すため。
  例外として、このアプリの別の画像が持ち主でも、それを最後に描いた Canvas が消えた・隠れたなら譲ってもらう(`FullColor::canvas` の WidgetId を引く)。
  ImageView はこの決まりを見ずに奪うが、奪われた Canvas は描き直しで譲るので交互にはならない。
- 透過のPNGは、フルカラーでは透過せず `background`(既定は白)と混ぜて描く(下に何が描いてあるかは液晶から読めないため)。
- 描く関数は ImageView と共通の `ImageView::PaintToLcd()`(画像の左上の位置・縮めた後の大きさ・クリップから、見えている窓だけを解いて送る)。
- 検証: `lua_ext_test`(頼む矩形・かからない描き直しでは頼まない・一部にかかれば全体・Canvasの外を切る・full_color無し・上に部品が載っている間・
  renderの外・覚えられる枚数を超えた・image_freeでやめる)。PCビルドで、Spotifyの画面と同じ描き方(`view.lua`)のジャケットがフルカラーで出ること、
  再生位置の欄を毎フレーム描き直してもフルカラーが残ること、上に Label を載せている間は16色・どけたらフルカラーへ戻ることを `--shot` で確認した。
  **実機の速さ(75x75のJPEGを描き直すのにかかる時間)は未確認(Todoist参照)**。
