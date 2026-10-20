# Luaアプリと pico.game(ゲーム・ストア・動作確認)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## ペイント(`pc/sdcard/lua/apps/ペイント/`、2026-09-24実装)

SUMMARY.md #8の「ペイント」。**Luaアプリ**で、描画の中身は全て`CanvasRaster`(C++)に足した
(Luaにはピクセルを読む口が無く、塗りつぶしをLuaで書くと遅すぎるため)。スクリプトは道具の切り替えと
ダイアログ(色=`ColorDialog`、保存=`FileSaveDialog`+上書き確認`MsgDialog`、開く=`FileSelectDialog`、
新規/未保存での終了=`MsgDialog`)の配線だけ。ツールバーはアイコンボタン2段
(道具: 戻る/ペン/消しゴム/直線/四角/楕円/バケツ、操作: 色/太さ/元に戻す/新規/開く/保存)。
**四角/楕円は選択中にもう一度押すと輪郭⇔塗りつぶし**(アイコンも塗りつぶし版に変わる)。

`CanvasRaster`へ足したもの:

- **`Canvas::Mode`に`Straight`(4)と`Fill`(5)を末尾へ追加**(Luaは数値で指定するので既存値は動かさない。
  `WidgetProperty`は範囲外の`canvas_mode`をエラーにする)。
- **図形は`drawShape()`の1箇所で描く**(ドラッグ中の`frame`へのプレビューと、離したときのスプライトへの
  焼き込みが同じ関数)。四角形は左上/右下へ揃えるので**どちら向きにドラッグしても同じ**(以前は右下へ
  ドラッグした場合しか正しくなかった)。輪郭は`brush_radius`の太さで、四角は4辺を`DrawThickLine()`、
  楕円は周を折れ線で近似して各辺を`DrawThickLine()`(`drawEllipse()`を半径をずらして重ねると縞状の
  隙間が残る)。`filled`プロパティで`fillRect()`/`fillEllipse()`。
- **プレビューのdirtyは「前回+今回の外接矩形」だけ**(`preview_rect`)。以前は動くたびに全体を
  `needsRender()`していた。プレビューは`frame`へ直接描くので、今のクリップ(FlushDirty()が掛けた
  dirty矩形)と自分の矩形の重なりへ絞ってから描く(`getClipRect()`で退避・復元)。
- **塗りつぶし(`floodFill()`)**: スキャンライン方式。種(シード)の置き場は固定長256件で、溢れたら
  「塗った画素に隣接する、まだ塗っていない同色の画素」を全面走査して拾い直す。**塗ったかどうかは色では
  判定できない**(元から塗り色だった画素と区別できない)ので1bit/画素の印を持つ。種と印は塗る間だけの
  一時確保(全面キャンバスで約7KB)。塗った範囲だけをdirtyにする。
  溢れる形(格子状の点等)は全面走査を何周かするので重い。実機での速度は未計測。
- **元に戻す(1段)**: `undo_enabled=true`の間だけ、スプライトと同じ大きさ(`bufferLength()`。4bppで
  `w*h/2`、ペイントの234x194で約23KB)の控えを`malloc`で持つ。描き込む直前(ペンは触れた瞬間、図形は
  離した瞬間、塗りつぶしは実際に色が変わるときだけ、`canvasClear()`、`keep_size`の読み込み)に
  `memcpy`で控え、`undo()`は**控えと中身を入れ替える**(2回目はやり直しになる。2枚持たずに済む)。
  既定は無効(スクラッチパッド等に負担させない)。`resize()`で控えは捨てて取り直す。
- **`pico.canvas_load(id, path, keep_size)`**: `keep_size=true`なら`resize()`せず白紙にしてから
  `DecodePimgBody()`する。**`writePixel()`はクリップされるので、大きい画像は右/下が切れるだけ**。
- `pico.show_file_save(start_dir, default_name)`(`FileSaveDialog::setFileName()`)。
- アイコン10種(pencil/line/circle/square-filled/circle-filled/bucket-droplet/palette/arrow-back-up/
  folder-open/file-plus)を**`ICONS`の末尾へ**足した(IconID 70〜79)。四角形の道具は既存の
  `CheckboxOff`(35、tablerの`square`)を流用している。

気づいたこと:

- **`LuaScene`が読むスクリプトは16KiBまで**(超えると切り詰めて構文エラー)。日本語コメントは1文字3Bで、
  ペイントは最初16.3KBあって踏んだ。`main.lua`冒頭に注意書きを残してある。
- **`Label`では`*`がマークアップとして消える**ので、未保存の印は「(未保存)」と書いている。
- ダイアログを閉じた直後に次のダイアログを開く流れ(保存→上書き確認、等)は、`closed`の中で直接開かず
  `loop()`で2フレーム待つ(`MarkdownScene::Pending`と同じ理由)。
- ホストテストのスタブ(`stubs/LovyanGFX.h`)は`fillRect`/`drawFastHLine`も実際に書き込むようにし、
  `bufferLength()`を足した。塗りつぶし(種あふれを含む)・元に戻す・塗りつぶしの四角形・`keep_size`は
  `lua_engine_test.cpp`で確認。輪郭の図形は`fillCircle`/`fillTriangle`がスタブで無描画なので、見た目は
  PCビルドの`--tap`で確認した。

## テトリス(`pc/sdcard/lua/apps/テトリス/`、2026-09-26実装、2026-10-07に`pico.game`へ移行)

SUMMARY.md #8の「テトリス風」。**本体はLuaアプリ**。作りは下記「ボードゲーム4本のpico.gameへの移行」(盤面=タイルマップ、落ちるミノ=スプライト)。

- ファイル: `main.lua`(規則・状態・見た目への写し方)/ `lib.lua`(ミノの形・SRSの壁蹴りの表・操作ボタンの絵`drawBtn`・HOLD/NEXTのミノの絵`mini`。
  LuaSceneが本体より先に実行しグローバル変数`LIB`で渡る。`main.lua`の16KiB制限は`pico.game`移行でゆとりができた)/
  `blocks.pimg` / `bgm.mml` / `hiscore.txt`(書き出し)/ `app.cfg` / `icon.pimg`。
- **ミノの絵は画像**: 12x12のタイルを横に10枚(I O T S Z J L ゴースト 消えるラインの白 空きマスの黒)並べた`blocks.pimg`(120x12)。
  盤面のタイルマップ・スプライト・HOLD/NEXTの`pico.draw_image_part()`の全部で同じ画像を使う。`script/assets/generate_tetris_blocks.py`がパレット番号で
  直接描く(PIL不要)。パレットに橙が無いのでLは灰。
- 規則: SRSの回転と壁蹴り(左回転は「1つ前の向きからの右回転」の候補の符号反転で引く)、7種1巡、NEXT3つ、HOLD(1個につき1回)、ゴースト、
  接地から500msで固定(動かすと猶予が戻る、15回まで)、左右の長押しは170ms後に50msごと、ソフトドロップ30ms/段(+1点)、ハードドロップ(+2点/段)、
  得点は100/300/500/800×レベル、10ラインごとにレベルが上がり落下はガイドラインの式(`(0.8-(lv-1)*0.007)^(lv-1)`秒)。消える行は200ms白く光る。
- **盤面**: 論理の盤面`grid`(見えない上2段を含む22段)を、`syncBoard()`が`map:set()`でタイルへ写す(`board_dirty`の間だけ。変わったタイルだけエンジンが描く。
  空きマスは黒のタイル(10)、消える行は白のタイル(9))。**落ちているミノとゴースト**は4枚ずつのスプライトで、`syncPiece()`が毎フレーム位置・コマ・表示を書く。
  HOLD/NEXT/点数は`g:on_draw`で描き、変わったとき(`side_dirty`)だけ`g:dirty()`。描き直す範囲に重ならなければ描かない(`pico.get_draw_area()`)。
- **画面**は`g:state`の5つ: title / play / pause / clear(ライン消しの光る200ms) / over。「停止」「戻る」と下の6つの操作ボタンは`g:button`
  (操作ボタンは`draw`で自前の絵。**名前をコントローラーのボタン名(`left` `down` `right` `up` `b` `a`)と同じにした**ので、画面のボタンでも十字キーでも同じ
  `g:down`/`g:pressed`で読める。押したまま指を滑らせると隣のボタンへ移るのもエンジンの機能)。
- 入力: タッチ(下の6ボタン、盤面タップ=右回転/開始/再開、HOLD枠・BGM枠のタップ)とコントローラー。十字キー=移動/ソフト(下)/ハード(上)、A・X=右回転、
  B・Y=左回転、L/R/ZL/ZR=HOLD、START=一時停止/開始、HOME・Esc=戻る(`pico.on_back`)。
  **HOLD/BGMのタップは左の欄(x<SX+54)だけ**(移行前は右の欄全体のキャンバスが反応していて、NEXTの絵をタップしてもHOLDが働いた)。
- 音: BGMは`bgm.mml`(コロベイニキ、ロシア民謡でパブリックドメイン)。効果音は**チャンネル2**(BGMが使わないチャンネル)で鳴らすので曲を借りない。BGM枠のタップで入り切り。
- 検証: `tetris_test`(run.sh。`script/host_test/tetris_test.lua`を`lua_script_test`(vendorしたLuaで.luaを動かすだけの下請け)で実行する。
  **`pico.game`は本物(`src/lua/builtin/game.lua`)で`pico.*`だけ偽物**。`main.lua`の末尾へ`TEST`(ローカル変数を覗く口)を足して読む。ライン消し・テトリス・
  壁蹴り・HOLD・長押し・ゲームオーバーとハイスコア保存・一時停止・タッチの操作ボタン(滑らせる)・重力・盤面のタイルとスプライトの位置・16KiB以内)、
  PCビルドの`--tap`で盤面・ミノ・ゴースト・HOLD/NEXTの絵(`PICOOS_VERIFY_LCD=1`で食い違い0)。**実機では未確認**。

## ブロック崩し / マインスイーパーの効果音・コントローラー(2026-09-26、2026-10-07に`pico.game`へ移行)

(盤面・画面の作りは下記「ボードゲーム4本のpico.gameへの移行」。ここは音とコントローラーの決まり)
- **ブロック崩し**(`pc/sdcard/lua/apps/ブロック崩し/`): 効果音はチャンネル2で**1フレームに1回、優先度の高いものだけ**鳴らす
  (`se(pri, ...)`で候補を覚え、`g:on_update()`の最後で1回`pico.sound_play`。ボールが16個あると命令の列(32件)が溢れるため)。
  壁<パドル/壊せないブロック<ブロック(色で高さが変わる)<アイテム。ミス/ステージクリア/全クリア/ゲームオーバーは
  `pico.music_play_text()`の短いジングル(チャンネル1なので効果音とぶつからない)。
  コントローラー: 左右=パドル(220px/秒)、A/START/上=発射・次へ・もう一度(ダイアログをやめて`g:state`の枠にしたので、タッチでもコントローラーでも同じ`goKey()`)。HOME・Esc=戻る。
- **マインスイーパー**: 開く(1マス=クリック/0マスで広く開く=低い音)・旗の立て外し・旗モードの切り替え・爆発(ノイズ)・クリアのジングル。
  効果音はチャンネル2(クリアのジングルの頭の音を食わないように)。

## 2Dゲームの簡易エンジン(`pico.game`、2026-10-06)

`require("pico.game")`。**本体はLuaの同梱モジュール**(`src/lua/builtin/game.lua`)で、C++へ足したのは重い描画の2つだけ:
`pico.draw_tilemap(handle, tw, th, data, cols, x, y)`(`LuaEngine_Ext.cpp`。1バイト1マスの文字列を今のクリップにかかるマスだけ
`DrawPimgSprite()`で描く。`draw_image_part`と同じ「クリップを狭めて画像全体をずらしてpushSprite」)と、
`pico.draw_image_part`の8・9番目の引数`flip_x/flip_y`(反転はpushSpriteでできないので1画素ずつ。`draw_image_ex`と同じ書き方)。
ドキュメントは`lua-api-doc/content/api/game.md`、サンプルは`pc/sdcard/lua/apps/ジャンプアクション/`(絵は`script/assets/generate_platformer_sheet.py`)。

- **構成**: `game.new{...}`がCanvasを1枚作り、`pico.every(1, ...)`で毎フレーム`g:step()`(`manual=true`なら自分で呼ぶ)。
  1回のdtは0.05秒で頭打ち(遅いフレームで壁をすり抜けないため)。順番: 入力 → ゲーム内タイマー → 状態の`update` → `g:on_update` →
  各スプライト(アニメ → `on_update` → 重力と移動)→ `g:collide`の規則 → カメラ → 描き直す範囲。
- **タイルとの当たり**: 軸ごとに動かし、動いた向きに**新しく入ったマスだけ**を近い順に見て押し戻す(すり抜けない・壁の中から始めても
  反対側へ飛ばない)。結果は`on_ground`/`hit_ceiling`/`hit_wall`(そのフレームだけ。`on_update`では前のフレームの値が読める)。
  坂は無い(すり抜け床は下の「軽い物理」)。スプライト同士はAABB(`hitbox`)の総当たり(`g:collide(tagA, tagB, fn)`)。
  **押し戻しの位置は「タイルの端 - 箱のずれ」で決める**(差し引きで直すと小数の誤差が溜まり、床の上でyが56.0000…01になって`==`が外れた)。
- **描き直し**: カメラが動いた/状態が変わった/重なり順が変わった → `pico.invalidate`。それ以外は見た目(位置・コマ・反転・色・表示)の
  変わったスプライトの前後の矩形だけを`pico.mark_dirty`(24個を超えたら全体)。`render`は`pico.get_draw_area()`の範囲にかかるものだけ描く
  (FlushDirty()はdirty矩形ごとに`render`を呼ぶため)。HUDは`g:on_draw`で描き、中身を変えたら`g:dirty()`で知らせる決まり。
- **入力**: `g:down/pressed/released(name)`はコントローラー(`pico.pad_*`)と画面ボタン(`g:button`、`pad=true`で下56pxに← ↑ ↓ → B A)をOR。
  画面ボタンは`press_start`で`latch`も立てて1フレームより短いタップを取りこぼさない(テトリスと同じ)。押したまま滑らせると隣へ移る。
- **同梱モジュールのデバッグ情報を落とすようにした**(`LuaEngine::preloadModules`→`StripFunction()`。`lua_dump(strip=1)`して`"b"`で読み直す。
  自分でdumpしたものなので安全)。pico.gameのrequireは約61KB→約47KB(PCの64bit。`lua_ext_test`が表示する。軽い物理を足して約55KB)。
  代わりに同梱モジュールの中で起きたエラーは行番号が「?」になる(`error(msg, 2)`の引数の誤りはアプリの行を指す)。
  pico.ui/async/tweenも同じく小さくなった。**実機(32bit)での値は未計測**(ポインタが半分なので少し小さいはず)。
- 検証: `lua_ext_test`(反転の画素・`draw_tilemap`の引数とクリップの復元・rows/legend/spawn・重力と床・壁・すり抜け・天井・hitbox・
  collideとremove・アニメ・状態・ゲーム内タイマーと一時停止・画面ボタン(押す/滑らせる/離す/短いタップ)・タイルの書き換え・
  描き直す矩形が動いたスプライトの前後だけ・カメラの追従と端・bounded・renderのon_draw/draw)。PCビルドの`--tap`でサンプルの
  タイトル・歩く・スクロール・ジャンプ・ミスを`--shot`で確認。**実機では未確認**(Luaで毎フレーム回す量・反転描画・タイル描画の速さ)。
- **軽い物理(2026-10-06)**: 全部Lua(C++は触っていない)。スプライトに`ax/ay`・`drag`(1/秒)・`friction`(床に乗っている間だけ、
  前のフレームの`on_ground`で判定)・`max_vx`・`bounce`/`bounce_min`(壁で止まる所は全部`stop_v()`を通る)・`mass`・`static`・`drop_through`、
  `s:impulse()`、`game.new{gravity=, iterations=}`(世界の重力は`gravity`未指定かつ`static`でないものだけ)。タイルマップの`oneway`
  (下へ動いて、動く前の足元がマスの上端以上だったときだけ乗る)。**`g:solid(tagA, tagB, fn|{oneway, on_hit})`がスプライト同士を押し合う**
  (`_physics()`、移動の後・`collide`の前): 動く前の位置(`_px/_py`、step()が各スプライトの更新前に覚える)から入ってきた軸を決め、
  押し戻しは逆質量の比。**押される向きが塞がっている側(hit_wall/on_ground/hit_ceiling)は動かないものとして扱う**(これが無いと
  「押す→箱→壁」が2周では収束せず、押している側が箱にめり込んだ)。速度は近づく成分だけ撃力で交換(bounceの大きいほう)。
  上に乗ったら`_ride`を覚え、次のフレームの頭で足場の移動量だけ`move()`する(動く足場)。規則はタグ別の配列を作って総当たり、
  `iterations`(既定2)周。コールバックは1周目だけ。検証は`lua_ext_test`の「pico.game: 物理」(跳ね返り・摩擦・抵抗・impulse・
  oneway・押す/壁/積み重ね・動く足場・速度の分け合い/入れ替わり/質量比・世界の重力)。**実機では未確認**(総当たりの命令数)。
  **上がる足場に乗ったものは`_ride`で運んだ後、足が足場の上面から0.5px以内なら`on_ground`/`_ride`を立て直す**
  (運ぶと重なりが生まれず、1フレームおきに`on_ground`が外れてジャンプが効かなくなった)。
- **サンプル「おしてのぼれ」(`pc/sdcard/lua/apps/おしてのぼれ/`、2026-10-07)**: 物理を使うパズルアクション3ステージ
  (`stages.lua`。1=木箱を押して踏み台・横の足場・すり抜け床、2=重い鉄箱(mass 4)でスイッチを押さえて扉を開ける・ばね、
  3=縦の足場で棚へ上がり、棚の箱を落として2段に積む)。絵は`script/assets/generate_physics_sheet.py`(地面・主人公・旗は
  `generate_platformer_sheet.py`から借りる)。スイッチは押さえている間だけ扉が開き、扉のマスに何かが重なっていれば閉じない。
  ばねは`g:solid(t, "spring", fn)`で`ny==-1`のとき打ち上げる(Aを離して低く跳ぶ処理は自分のジャンプのときだけ。
  ばねの勢いまで切っていた)。押す速さは箱の`friction`で決まる(木箱400で約55px/秒、鉄箱150で約30px/秒)。
  足場は端で0.8秒止まる(止まらないと乗り込めなかった)。検証は`oshite_test`(run.sh。`lua_script_test`で本物のpico.game
  (`src/lua/builtin/game.lua`を読む)と偽物のpico.*を使い、操作を流し込んで3ステージを実際に解く。箱が無いと/1つでは
  壁に届かないことも見る)とPCビルドの`--tap`/`--shot`。**実機では未確認**。
- 未: 坂・スプライトの回転/拡大(`draw_image_ex`を`s.draw`で使えばできる)・パーティクル・効果音の補助・
  タッチパネルでの同時押し(XPT2046が1点しか取れない)。

## ボードゲーム4本の`pico.game`への移行(リバーシ・マインスイーパー・テトリス・ブロック崩し、2026-10-07)

4本とも、ウィジェット(`Button`/`Label`/`Canvas`/`Rect`/`Ellipse`…)を自分で作る書き方をやめ、`game.new`が作る`Canvas`1枚の中で
**画面ボタン(`g:button`)・状態(`g:state`)・タイルマップ・スプライト**だけで組み立て直した。ゲームの規則(盤面の判定・SRS・速さの段・ステージ定義等)は
そのまま。**盤面はどれもタイルマップ**で、「1マス=1タイル、石/旗/ブロック/ミノの違いはタイルの値」にしたので、変えたいマスを`map:set()`するだけで
エンジンがそのマスだけ描き直す(マインスイーパーの0マスの連鎖も、ブロック崩しのブロックを壊すのも同じ)。絵は`script/generate_*.py`が
パレット番号で直接描く(PIL不要、`generate_pimg.py`と同じ.pimg形式)。

| アプリ | 盤面 | 動くもの | 画面(`g:state`) | 画像 |
|---|---|---|---|---|
| リバーシ | タイルマップ8x8(24px。1=空 2=黒 3=白 4=打てる印) | なし | play / pass / over | `tiles.pimg`(96x24) |
| マインスイーパー | タイルマップ(難易度ごとに20/16/14px+隙間。1=閉じ 2=空き 3〜10=数字1〜8 11=旗 12=地雷) | なし | menu / play / done | `tiles20/16/14.pimg` |
| テトリス | タイルマップ10x20(12px) | ミノ・ゴースト=スプライト4枚ずつ | title / play / pause / clear / over | `blocks.pimg`(120x12) |
| ブロック崩し | タイルマップ8列(29x14px。1〜6=速さの段 7=壊せない灰) | ボール・パドル・アイテム=スプライト | ready / play / clear / over / win | `tiles.pimg`(203x14) |

- **エンジンへ足したもの(`src/lua/builtin/game.lua`)**: 画面ボタンの`visible = false`(描かず、**タッチも受けない**。画面ごとに見せるボタンを入れ替えるのに。
  それまで`button_at()`は`visible`を見ていなかった)と、ボタンの`draw = function(b, x, y, w, h, on)`(自前の絵。テトリスの操作ボタン・ブロック崩しの「自動」)。
  `pico.game`のドキュメント(`lua-api-doc/content/api/game.md`)と、4本の解説(`examples/tetris.md`・`examples/board-games.md`)を更新した。
- **ダイアログをやめて`g:state`の枠にした**: リバーシ(パス・結果)とブロック崩し(ステージクリア・ゲームオーバー・全クリア)は`pico.show_message`だった。
  今は盤面の真ん中へ`draw`で枠を出し、タップ/A/STARTで進む(パスは1.8秒でも進む)。コントローラーでも同じ操作になり、`closed`の自前処理(`pico.destroy`して自分でfnを呼ぶ)が要らなくなった。
- **マインスイーパーの難易度**は`TabBar`から、最初の`menu`の状態(大きな3ボタン)と、上の「難度」ボタンで`menu`へ戻る形に変えた。選び直すたびに`g:clear()`してタイルマップを作り直す
  (マスの大きさが違うため。画像は難易度ごとに1枚。`digits/flag/mine.pimg`は`script/assets/generate_minesweeper_tiles.py`に文字で埋め込んで消した)。
  上級が300px(コンテンツ領域)にちょうど収まるよう、見出しの高さは58px。
- **ブロック崩しのボール**の位置と速さは自前(`cx` `cy` `dx` `dy`。**`vx`/`vy`にするとエンジンが勝手に1回で動かす**)。1フレームを6px以下に刻んで壁・ブロック・パドルへ当てる
  (エンジンの1回の移動だとブロックをすり抜ける)。ブロックとの当たりは外接矩形がかかるマスだけ`map:get(c, r)`で引く(以前は全ブロックを回していた)。
  アイテムは`vy`だけ持たせてエンジンが落とし、パドルとの重なりは`g:collide("paddle", "item", fn)`。ステージの切り替えは`g:clear()`で全部消して作り直す。
  「自動」(バッテリー計測用の放置プレイ)は残してある。`lib.lua`の`itemShapes`/`makeAutoToggle`/`autoControl`は不要になり消した(`autoTargetX`だけ残る)。
- **罠(ブロック崩しで踏んだ)**: **スプライトの`draw`は`w`x`h`の外へはみ出させない**。ボールを`pico.fill_circle(x+4, y+4, 4, 0)`(直径9画素)で描きながらスプライトを8x8にしたら、
  はみ出した1画素が消去の範囲(スプライトの矩形)に入らず、毎フレーム残って軌跡になった(`PICOOS_VERIFY_LCD=1`は「液晶=frame」を比べるだけなので、frameの時点で
  残っているこの種の汚れは検出できない。`--shot`で動かして目で見ること)。`fill_circle(x, y, r)`は直径`2r+1`画素なので、枠を9x9(ボール)・11x11(アイテム)にした。
- **`g.touch`の座標はキャンバスの中**(`x`/`y`)で、画面ボタンの上のタッチは含まない(ボタンは別に`g:pressed(名前)`)。キャンバスをステータスバーの下の全面
  (`pico.content_rect()`)にしたので、リバーシ等の座標はステータスバーの高さ(20px)を引いた値で書いてある。盤面の外のタップは各アプリが範囲を見て捨てる。
- **HUD(点数・状態の行)は`g:on_draw`で描き**、中身が変わったときだけ`g:dirty(x, y, w, h)`。キャンバスは背景色で塗ってから描き直すので、前の文字は自動で消える。
- 検証: **`reversi_test`/`minesweeper_test`/`breakout_test`を新設し、`tetris_test`を書き直した**(どれも`run.sh`。`pico.game`は本物、`pico.*`だけ偽物。
  `main.lua`の末尾へ`TEST`を足して中身を覗く。乱数は`math.random`を差し替えて地雷やアイテムを決め打つ)。
  リバーシ=打つ・裏返す・パス(タップ/時間)・終局・リセット中のパス、マインスイーパー=最初の1手は安全・連鎖・旗・地雷・3難易度・難度ボタン、
  ブロック崩し=壁/ブロック/パドルの反射・段ごとの速さ・点・アイテム3種・残機・ステージクリア・全20ステージ・全クリア・自動プレイ。
  PCビルドの`--tap`/`--shot`で4本とも動かして見た目を確認した(`PICOOS_VERIFY_LCD=1`で食い違い0)。**実機では未確認**(Luaで毎フレーム回す量・タイルマップの描画の速さ)。

## アプリストア(`src/apps/store/` / `StoreScene` / `docs/APP_REPO.md` / `script/build_app_repo.py`)(2026-10-11)

ランチャの「ストア」(`StoreScene`、アイコン`IconID::AppStore`)が、Webに置いた配布物からLuaアプリを入れる・更新する・消す。
**形式は静的なファイルだけ**(catalog.tsv + catalog.sig + `<id>/<版>/files.tsv` + ファイル)なので、GitHub Pages でも
Raspberry Pi の nginx でも同じ。仕様は`docs/APP_REPO.md`。既定のURLは`https://kimu1109.github.io/pico-os/apps/`(`/sys/store.cfg`の`url`)。

- **守り方**: カタログ(catalog.tsv)だけにEd25519で署名し(`"pico-os-apps-v1\0"`+SHA-256(catalog))、そこから
  files.tsv(SHA-256)→各ファイル(SHA-256)とつなぐ。鍵は`src/apps/store/App_Repo_Keys.hpp`の`kBuiltin`(今は空の目印1件=全部0。
  全部0の鍵は信頼しない)か`/sys/store.cfg`の`key = 16進64桁`(4行まで。SDへ書ける人はアプリを直接置けるので弱くならない)。
  **秘密鍵は利用者のPCにだけ置く**(`build_app_repo.py keygen`。リポジトリ・サーバ・GitHubのSecretsにも置かない)。
  同じURLで前に見た`serial`より小さい一覧は断る(`/sys/store/state.cfg`。古い署名済みの一覧へ戻される攻撃)。
  app.cfgの`id`・`version`がカタログと違う、または**権限がカタログの`perms`を超える**ものは入れない(入れる前に`perms`を確認のダイアログで見せる)。
  署名の確認はMonocypherの`crypto_ed25519_check`(SSHと同じvendor)。署名側の`build_app_repo.py`はRFC 8032を素のPythonで書いたもの
  (RFCのテストの鍵で一致、Pythonの署名をMonocypherで確かめるのは`app_repo_test`)。
- **AppStore(`apps/store/App_Store`)**: `HttpRequest`1本をkeep-aliveで使い回す(HTTPSのハンドシェイクを1回に。画面を離れるとき`close()`)。
  受け取りながらSDへ書いてSHA-256を取る(`FileSink`、大きさの上限付き)。catalog.tsvは`/sys/store/catalog.part`へ取り、確かめてから
  `catalog.tsv`へ差し替える(失敗しても前の一覧のまま)。インストールは`/sys/store/staging/`へ全部取って確かめてから、
  **ファイルを1つずつ**`/lua/apps/<dir>/`へ移す(ディレクトリごと移さないのは、更新でアプリが書いたセーブ等を残すため)。
  前の版の一覧(`/sys/store/installed/<id>.tsv`)にあって新しい版に無いファイルだけ消す。files.tsvは全体をRAMに載せず、
  1行ずつ読み直す(512バイトの行バッファもスタックではなくメンバ)。カタログ40件で**約16KB**あるので、画面が開いている間だけ`new`する。
  `Installed`: No / Same / Update / Newer / Foreign(同名のディレクトリにidの無いアプリ=手で入れたもの。上書きの確認を出す) / Conflict(別のid。入れない)。
- **登録簿**: インストール後すぐランチャに並ぶ(`LuaAppScanner::RegisterOrRefresh()`。既に登録済みなら`AppFunctions::Refresh()`で名前・権限・アイコンだけ差し替え、
  並びの位置は変えない)。削除は`AppFunctions::Unregister()`(後ろのindexを詰め、ランチャの並び・隠した印も詰める)。
  `LuaAppScanner::ReadAppConfig()`/`AppInfo`を外へ出した(app.cfgに`id`を足した。走査は`.`で始まるディレクトリを飛ばすようにした)。
  **登録簿の上限は32件のまま**(静的15+SDのLuaアプリ12で27。ストアで5本より多く入れると、超えた分は次の起動のスキャンでも並ばない。
  `launcher_hidden`が32bitなので、広げるならそこも)。
- **配るアプリ**: `pc/sdcard/lua/apps/*/app.cfg`に`id`があるもの(12本全部に付けた)。gitの管理下ならgitが管理しているファイルだけ。
  ファイル名はASCIIだけ(`[A-Za-z0-9._-]`、4段・95文字)、ディレクトリ名は日本語可(catalog.tsvの`dir`)。
- **GitHub Pages**: 署名した目録(`build_app_repo.py sign --out store`)を`store/`にコミットすると、`web-pages.yml`が`assemble`で
  `pc/sdcard/lua/apps/`から中身を写して`_site/apps/`へ置く(**秘密鍵はCIに要らない**)。署名の後で中身が変わったアプリは警告して配らない
  (`--allow-stale`。Webの公開そのものは止めない)。**`store/`はまだコミットしていない**(利用者の鍵で署名する必要があるため)。
  github.io の証明書の発行元が焼き込みのルート(DigiCert/Sectigo系)で足りるかは実機で未確認。
- 検証: `app_repo_test`(run.sh の core。行の読み取り・名前とパスの検査・版・権限・署名)、`app_test`(Refresh/Unregister/FindByArg)、
  **`app_store_net_test`(run_net.sh。`app_store_fixture.py`が配布物の変種を作り、`python3 -m http.server`が同じURLで中身を張り替えて配る)**:
  一覧・インストール・手で入れた同名のアプリへの上書き(アプリのファイルが残る)・知らない鍵/壊れた署名/鍵の設定なし・更新(消えたファイル・増えたサブディレクトリ・
  セーブが残る)・古い一覧・改ざんしたファイル・一覧より強い権限・削除・404・中止。PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0)で
  一覧→確認→インストール→開く→削除(実ファイルシステムでディレクトリが消える)。**実機・GitHub Pages・本物のHTTPSでは未確認**。

## 動作確認アプリ(`pc/sdcard/lua/apps/動作確認/`、2026-10-10)

APIごとに増えていた動作確認用のアプリを1つにまとめた。`main.lua`はメニューで、各ページ(同じフォルダの`*.lua`)を
`pico.push_scene`で開き、ページの「戻る」(`pico.pop()`)でメニューへ戻る。権限(`app.cfg`。通知のため`permission_notify=true`)は
`push_scene`で開いたページへ引き継がれる。ドキュメントは`lua-api-doc/content/examples/test-app.md`。

| ページ | ファイル | 元のアプリ |
|---|---|---|
| 基本の部品 | `basics.lua` | 「スキャン確認」「図形デモ」「Lua Hello」の部品、C++の「入力テスト」(Textbox/NumberInput/テスト音) |
| 追加の部品 | `extra.lua` | 「ウィジェット追加確認」 |
| 直接描画 | `draw.lua` | 「描画API確認」 |
| 画面とデータ | `scene.lua` + `picker.lua` + `util.lua` | 「ウィジェットAPI確認」 |
| コントローラー | `pad.lua` | 「コントローラー確認」 |
| 音 | `sound.lua` + `demo.mml` | 「チップチューン」 |
| 通知 | `notify.lua` | 「通知テスト」 |

- **消したもの**: `pc/sdcard/lua/hello.lua`/`hello_sub.lua`/`hello_sub2.lua`と`pc/sdcard/img/hello.pimg`(`App_List.cpp`の「Lua Hello」の静的登録ごと。
  `push_scene`は「画面とデータ」、`change_scene`/`launch_app`はホストテスト`lua_scene_test`で確かめている)、
  C++の`InputTestScene`(「入力テスト」。中身はLuaの「基本の部品」で足りる)、「スキャン確認」「図形デモ」(SDの走査は他のアプリ全部が実演している)。
  **この節より前の記述(検証手順の`--tap`の座標や「入力テスト」「hello.lua」での確認)は当時の記録**で、今は無い画面を指している。
- `pico.launch_reason()`はアプリの最初の画面(メニュー)で1回だけ読めるので、メニューが受け取って通知のページへ`args`で渡す。
- 新しいAPIの動作確認は、アプリを増やさずにこのアプリへページを足すこと(`main.lua`の`PAGES`へ1行)。
  メニューは1列のボタンで、あと1〜2ページ足すと画面に収まらなくなるので、そのときは`ScrollList`にする。
- 検証: PCビルドの`--tap`/`--shot`で全ページの表示、「基本の部品」の図形タブ、ページの「戻る」でメニューへ戻ること、
  通知を出して通知センターから開くとメニュー→通知のページ(`tag`/`data`付き)へ進むことを確認。
