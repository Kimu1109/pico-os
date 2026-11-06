# 速さとメモリ: 解決済みの不具合

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `performance.md`(「シーンアリーナ」の後ろ)から分けた。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## ✅ 解決済み: PCビルドでシーン遷移を繰り返すと「ヒープ下限」が際限なく増える(2026-09-19発見 → 2026-09-28特定・修正)

上の結論は**実機RP2350での計測**に基づくが、`LuaScene`の動作確認中、**PCビルド
(`pc/build/picoos_pc`)で`--tap`により同じ画面遷移を繰り返すと、上と同じ「ヒープ下限」
指標が回数に比例して際限なく増え続ける**ことを見つけた。**Luaとは無関係の、既存の
`InputTestScene`だけでも再現する**(検証方法・結果は次の通り):

```sh
SDL_VIDEODRIVER=dummy ./pc/build/picoos_pc \
  --tap 178,65@10:5  --tap 40,215@30:5  \
  --tap 178,65@60:5  --tap 40,215@80:5  \
  --tap 178,65@110:5 --tap 40,215@130:5 \
  --tap 178,65@160:5 --tap 40,215@180:5 \
  --tap 178,65@210:5 --tap 40,215@230:5 \
  --shot /tmp/probe.ppm 260
# (178,65)=ランチャの「入力テスト」タイル、(40,215)=InputTestSceneの「戻る」ボタン。
# ランチャ→InputTestScene→ランチャ を5回繰り返すだけ
# (このコマンド自体が「未解決」当時の再現コマンドで、原因のSDL_VIDEODRIVER=dummyを
#  わざと使っている。通常の動作確認ではdummyではなくoffscreenを使うこと。上記
#  「PC / Web実行環境」参照)
```
結果: `ヒープ下限(シーン破棄直後/9回): 初回=188032B 最新=478256B 最大=478256B 差+290224B`
(9サンプルで約290KB増加。1往復あたり約30〜40KB)。

**この現象がLua着手より前から存在することも確認済み**: このセッションでの変更を一切含まない
`git worktree`(コミット`4f8310a`、Lua関連の作業を始める直前)でも全く同じ手順・ほぼ同じ数値
(`初回=188320B 最新=480848B 差+292528B`)が再現した。つまり**`LuaEngine`/`LuaScene`が
原因ではない**(`script/host_test/lua_engine_test.cpp`/`lua_scene_test.cpp`はASan付きの
ホストテストで、Push/Pop相当のシナリオを含めて一切のリークを検出していない。今回の増加は
ASanの通らないPCビルドのGUI経路——実際のLovyanGFX描画・フォント処理・Task/Networkの
どこか——で起きている)。

**実機での20回計測(2026-09-12、上記)と矛盾しているように見える点に注意**: 実機計測は
`sh script/host_test/run_mem.sh`(ASan相当ではないがWi-Fi/描画も含めた実機実行)による
もので、その時は増加傾向が無かった。今回の再現条件(同一シーンの往復を`--tap`で機械的に
繰り返す/PCビルド固有のSDL・glibc・LovyanGFX_SDLパネル経路)との違いが原因の可能性があり、
**実機で同じ「同一画面の往復を10回以上」というシナリオを踏んだことは無い**(実機計測は
「シーンを跨いだ複数回の遷移」であり、「同じ2画面の往復」ではなかった)。

**手がかり(2026-09-24)**: 増え方は**往復の回数ではなく、シーンに滞在したフレーム数に比例する**
(1フレームあたり約1.3〜1.4KB)。同じ`InputTestScene`で「戻る」を押すまでを40フレーム→400フレームに
延ばすと、残留が約49KB→約523KBになった。`GameBoyScene`でも、ROMを読まずにROM選択のダイアログを
開いたままにしているだけで同じ割合で増える。**どの画面でも毎フレーム何かが確保されて返っていない**
(= シーン遷移そのものではなく、毎フレームの処理のどこか)と見てよい。

**未着手(2026-09-24時点)**: 原因の特定(候補: LovyanGFXのフォント/グリフキャッシュ、Task_Functions、
Network_Functionsの再接続チェック、SDL側のイベント処理)、実機での再現確認、修正。
  `Label::lines`の件は上記のとおり対処済みで、残る`std::function`のDelegate化も単体では5%程度の効果しかない
  (上記)。

**原因特定・修正(2026-09-28)**: 「候補」に挙げていた**「SDL側のイベント処理」が的中**。
`valgrind --tool=massif`で400フレーム分のヒープ成長を追跡したところ、増加分の一部が
`lgfx::v1::Panel_sdl::sdl_create() <- sdl_update() <- _update_proc() <- loop()`という、
**毎フレーム呼ばれる経路から確保されたまま残っている**ことが分かった(`PICO_GFX::Setup()`
からの起動時1回きりの確保とは別の経路として計上されていたのが決め手)。

原因は**pico-osではなくLovyanGFX側(`src/lgfx/v1/platforms/sdl/Panel_sdl.cpp`)のバグ**:
`sdl_update()`は`monitor.renderer == nullptr`の間ずっと`sdl_create()`を呼び直すが、
`sdl_create()`は`SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)`
の戻り値をチェックしていない。**この再現コマンドが使っている`SDL_VIDEODRIVER=dummy`は
この組み合わせのレンダラーを一切提供できない**(検証用の最小SDLプログラムで確認:
`dummy`は`opengl`/`opengles2`/`software`の3種を持つが、`index=-1`の自動選択では
先に列挙される`opengl`/`opengles2`の失敗で終わり、`software`まで辿り着かない。
`SDL_RENDERER_SOFTWARE`を明示すれば`dummy`でも成功する)。そのため`monitor.renderer`が
永遠に`nullptr`のままになり、`sdl_create()`が毎フレーム(実測: 60フレームで510回)
呼ばれ続け、**呼ばれるたびに`SDL_CreateWindow()`で新しいウィンドウを作り、直前の
`m->window`を`SDL_DestroyWindow()`せずに上書きして捨てる**(=毎フレーム1つ
SDL_Windowがリークする)、という壊れ方だった。

**この問題はSDL_VIDEODRIVER=dummyというヘッドレス実行条件に固有**で、実際のウィンドウ
表示環境(通常のPCビルド)・Webビルド(Emscripten、別経路)・実機(RP2350、SDL不使用)
のいずれでも再現しない。上の「実機での20回計測と矛盾しているように見える点」は、
実機がそもそもSDLを使わないことを踏まえれば矛盾ではなかったことになる。

対応:
1. **`pc/patches/0001-panel_sdl-renderer-fallback-and-cleanup.patch`**を新設し、
   `pc/CMakeLists.txt`のFetchContentへ`PATCH_COMMAND`として組み込んだ(取得した
   LovyanGFXへ自動適用される。詳細は`pc/patches/README.md`)。`sdl_create()`の
   呼び直しに備えて前回分を必ず`SDL_Destroy*`してから作り直すようにし、
   `SDL_RENDERER_SOFTWARE`へのフォールバックも追加した。これで`dummy`環境でも
   毎フレームのリークが止まり(検証: 上記`--tap`再現コマンドで
   `ヒープ下限(9回): 初回=276832B 最新=282880B 差+6048B`まで縮小。修正前は
   `差+523872B`)、`dummy`のままでも正しく描画できるようになった。
2. **`pc/README.md`/`CLAUDE.md`のヘッドレス確認コマンドは`SDL_VIDEODRIVER=dummy`
   ではなく`SDL_VIDEODRIVER=offscreen`を使うよう変更した**(`cli-tool/picoos`の
   `pc shot`サブコマンドも同様)。`offscreen`は最初からレンダラーを作れるので
   `sdl_create()`は起動時の1回だけで済み、パッチが無くても問題を踏まない
   (パッチは主に「`dummy`をどうしても使う場合の保険」「本当にレンダラーが
   1つも無い環境でもクラッシュしない安全策」という位置づけになった)。
3. 修正後に残る微小な残留(**+6048B/9回、1往復あたり1KB未満**)は、実機計測
   (2026-09-12、「9回時点で+248B」)と同程度のオーダーで、通常のシーン滞在中の
   一時確保(Wi-Fi再接続チェック等)が拾われているだけと見てよい。この規模の残留を
   追う投資対効果は低いと判断し、追加調査は行っていない。

**回帰テスト(2026-10-30)**: `scene_cycle_test`(ホストテストの widget。CIで回る)が、本物のシーン15種(時計・ファイル・通知・電卓・メモ・テキスト・辞書・
カレンダー・TODO・読書・Markdown・ストア等)を `Change` と `Push`/`Pop` で12周往復し、2周目の後を基準に、空の画面へ戻った瞬間の
ウィジェットの生存数・バイト数が1つも増えないこと・シーンアリーナの塊を全部返していること・ヒープの使用量(ASanの
`__sanitizer_get_current_allocated_bytes()`)が1周あたり64Bより増えないことを確かめる。上のSDLの件そのもの(LovyanGFXの中)は
捕まえられないが、過去に何度も起きた「デストラクタで子を消し忘れる」型は捕まる(わざと200Bを毎回残すと、ASanのリーク検出と
このテストのヒープ下限の両方で落ちることを確かめた。到達できるまま溜まる型はこのテストだけが捕まえる)。
初めの `printf` が標準出力の4KiBのバッファを確保して基準に混ざったので、出力はバッファ無しにしてある。

## ✅ 解決済み: 同じ`pc/build`を2回目以降configureすると必ず失敗する(2026-09-28発見・修正)

`cli-tool/picoos`へ`midi2mml`サブコマンドを配線した直後、ユーザーが`picoos pc build`を実行したところ
(=既に一度ビルド済みの`pc/build`に対して`cmake -S pc -B pc/build`を**2回目として**実行したところ)、
上記パッチの適用(`git apply`)が`error: patch does not apply`で失敗し、configureそのものが止まった。

- **原因は`FetchContent_Declare(lovyangfx ...)`側の設定漏れ**で、パッチの中身とは無関係。
  git系のFetchContentは既定で**「update」ステップ(git fetch + 対象タグへのcheckout)を
  configureのたびに実行する**。取得先のタグは固定なので中身は何も変わらないが、
  update自体が走ったという事実だけでmake/ninjaの依存関係上「patchステップより新しい」
  扱いになり、**既に当たっているパッチを同じ木へもう一度当てようとして失敗する**
  (`git apply`は冪等ではなく、当たり済みのhunkを検出して拒否する)。
  1回目のconfigure(まだ何も取得していない状態)ではupdateとpatchが同時に初めて走るため
  問題が起きず、**2回目以降のconfigureで必ず踏む**という踏み方だった。
- **対処**: `FetchContent_Declare`に`UPDATE_DISCONNECTED TRUE`を追加。これで初回の取得・
  パッチ適用が済んだ後はupdateステップ自体が自動実行されなくなり、同じ`pc/build`への
  再configureが安全になる(`pc/CMakeLists.txt`にコメントで経緯を残してある)。
- 検証: `picoos pc build --clean`(まっさらな状態から初回ビルド)→`picoos pc build`
  (2回目、**修正前ならここで必ず`patch does not apply`が出ていた**)→もう一度`picoos pc build`
  (3回目)の3連続実行が全て成功し、2回目・3回目は`update`/`patch`ステップ自体が
  走らずconfigureが一瞬(0.2秒程度)で終わることを確認。`--shot`での画面描画も
  修正前と変わらないことを確認済み。
- **LovyanGFXの版(`LOVYANGFX_TAG`)を上げたいときは`pc/build`を作り直すこと**
  (`UPDATE_DISCONNECTED`はconfigureのたびの自動update自体を止める設定なので、
  版を変えても既存の`_deps/lovyangfx-src`はそのまま使われてしまう。
  `picoos pc build --clean`か`rm -rf pc/build`で対応する)。
