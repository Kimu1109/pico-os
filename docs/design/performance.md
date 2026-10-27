# 速さとメモリ(最適化の記録とメモリ計測)

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## 最適化の記録

### OSのCPU・RAMの無駄を削る (2026-10-10)

OS側の洗い出しの結果、次を直した。実機ファームの静的RAMは 140,000B → 113,540B(-26.5KB)。

- **合成の外の描画を捨てる(二重描画の解消)**: `WidgetFunctions::UpdateAll()`→`Widget::update()`→`render()`で、変化したウィジェットが
  `frame`へ実際に描き、直後の`FlushDirty()`が背景ごと塗り直してもう一度描いていた。今は`Widget::update()`が`render()`の間だけ
  **frameのクリップを空にし**(書き込みは全部捨てられる)、`PICO_GFX::render_suppressed`を立てる。文字を描く`Label`/`Button`/`Statusbar`は
  これを見てdirtyを積むだけで戻る(グリフの展開も省く)。**新しいウィジェットの`render()`は、合成の外の回でも状態の確認とdirtyの積み上げは
  今まで通り行い、重い描画(文字・画像の展開)は`render_suppressed`なら省いてよい**。描画の結果を前提にした処理(frameを読み戻す等)を
  合成の外の回に置かないこと(空のクリップで何も描かれていない)。合成の中(`isDirtyDeactivates`)で状態の変化を見つけて覚えると
  `MarkDirty()`が効かず描き直しが失われるので、`Statusbar`のように変化の確認は合成の外の回だけで行う。
- **`MemFunctions::Update()`は`mallinfo()`を`kPeakSampleFrames`(32)フレームに1回だけ**(空きブロックを全部たどるので毎フレームは重い。
  PCビルドでは1フレーム約0.13ms食っていた)。
- **`FlushDirty()`の`hit`、`HitTest()`の作業用の`std::vector`は静的に使い回す**(フレーム/タッチごとのヒープ確保をやめた)。
- **ステータスバーは変化したときだけ描き直す**(時刻は分が変わったとき、電波・電池・SDは5秒ごとに見て段階が変わったとき)。
- **ログ**: `log.txt`は起動のたびに消さず追記し(区切りに`----- boot -----`)、起動時に256KBを超えていれば`/sys/log.old.txt`へ1世代回す。
  以前の`preAllocate(64KB)`+`truncate(0)`はやめた(truncateが確保した領域を返すので意味が無く、前回のログも消えていた)。
  バッファは4KB→1KB、`Log()`のスタックは768B→320B(1回だけ整形する)。
- **アプリ登録簿**: `AppEntry::icon_path`(96B)を`icon_file`(argのディレクトリからの相対パス、32B)にした。絶対パスは
  `AppFunctions::IconPathOf()`で組み立てる。`Register()`は従来どおり絶対パスを受け取り、アプリのディレクトリの外・32Bに収まらない
  ものは既定アイコンへ落とす。
- **IMEの索引**(最大約6.8KB)は`IME_Functions::Setup()`ではパスを覚えるだけにし、**初めて引いたときに件数ぶん`malloc`**、
  日本語のキー盤が見えなくなったとき(`KeyboardPanel::onPanelHidden()`)に`IME_Functions::Release()`で返す。辞書が無いときの失敗は
  返すまで覚えて毎回探さない。
- `FlushDirty()`の行のハッシュを64bit→32bit(5KB→2.5KB)。
- `Label::utilityInstance()`(テンプレートの種類ごとの約650Bの静的なLabel×3)をやめ、`DrawPlain()`等は`FontFn`と`setTextColor()`を直接呼ぶ。
- **`UpdateAll()`は要るウィジェットにだけ`update()`する**: タッチのあったフレーム(押した/離した/外を押したを全員へ配る)と、
  `getNeedsRedraw()`・`is_pressing`・**`wantsFrameUpdate()`**が真のウィジェットだけ(クリップの計算もそのときだけ)。
  `render()`の中で毎フレーム外の状態や時間を見張るウィジェットは`wantsFrameUpdate()`で真を返すこと(今は点滅カーソルのLabel・
  Statusbar・PerfOverlay・`redraw_below_frames`中のKeyboardDialog・長押し中のDurationPicker)。値の変化をsetter(`needsRender()`)で
  知らせるだけのウィジェットは何もしなくてよい。**見張りを`render()`に足したのに`wantsFrameUpdate()`を足し忘れると、
  タッチか描き直しの要求があるまで見張りが止まる**。
- **自動調光で暗くなっている間は約30fps**(`PowerFunctions::kDimFrameMs`=33ms。以前は100fpsのまま)。`KeepAwake()`を呼ぶ画面・
  音が鳴っている間・Wi-Fiの接続中(`Busy()`)は落とさない。
- クラッシュダンプの文章のバッファ(2KB/3KB)は書き出す間だけ`malloc`する(Luaのエラーで確保できなければ保存を諦める)。
  TextViewの全角の文字幅の控え(約2KB)はTextViewが1つでも生きている間だけ持つ(確保できなければ毎回測る)。
- **lwIPの領域(`PBUF_POOL`約36KB・`ram_heap`16KB)は減らせなかった**: arduino-picoはlwIPをビルド済みの`liblwip.a`で配っていて、
  プールの大きさ(`include/lwipopts.h`の`PBUF_POOL_SIZE`/`MEM_SIZE`)はそのビルド時に決まる。こちらのビルドフラグでは変わらず、
  変えるにはlwIPをソースからビルドし直してフレームワークのものと差し替える必要がある(TLSの受信の速さにも効くので見送った)。
- 検証: ホストテスト全部(`app_test`にアイコンの相対パス、`key_input_test`に索引の読み直し、`notification_test`は件数に依らない形へ)、
  PCビルドの`--tap`/`--shot`(`PICOOS_VERIFY_LCD=1`で食い違い0: ランチャ・SDのアイコン・電卓・テキストエディタ+日本語キー盤)、
  実機ファームのビルド。`power_test`に暗いときの間隔。**実機での速さ・見た目は未確認**。

### 液晶への転送を減らす(`GFX_Functions::FlushDirty()` / `LuaCanvas` / `gui/ScopedClip.hpp`) (2026-10-06)

「pico.gameのゲームが24fps出ない」の調査。PCビルド(SPIの待ちを実機と同じ速さで真似る)で、カメラが動き続ける場面を
測ると**1フレームに約15万画素(画面2枚ぶん)を送って11fps**だった。直したもの:

- **`LuaCanvas`が1フレームに2回描いていた**: `WidgetFunctions::UpdateAll()`の`Widget::update()`→`render()`でも
  Luaの`render`を呼んでいて、合成の外なので`pico.draw_*`が描いた範囲をそれぞれdirtyに積み(タイルマップで全面、
  ボタン1つずつ…)、`FlushDirty()`でもう一度描いて送っていた。`GameBoyView`と同じく、**合成の外(`!isDirtyDeactivates`)では
  自分の矩形をdirtyに積むだけ**にした。
- **`FlushDirty()`**:
  - 先に**重なる/近いdirty矩形を1枚にまとめる**(`CoalesceDirtyRects()`。和の面積が2枚の合計+1024画素以下なら1枚に)。
    同じ矩形が2回積まれると2回描いて2回送っていた。まとめると矩形同士が重ならないので、**全部描いてから全部送る**順にした。
  - **変わっていない行は送らない**(`PushChangedRows()`)。`FlushDirty()`の後は液晶の中身がframeと同じ、という前提で、
    送った行のframeの中身のハッシュ(64bit、`row_hash[320]`)を覚え、次にdirtyになってもハッシュが同じ行は飛ばす。
    スクロールで空だけの行、動いていないボタンの帯、描き直しても同じ絵の所を送らない。3行以内の切れ目は帯を分けずに送る。
    `MarkDirtyBelow()`の矩形は必ず送る(Luaデバッガが液晶へ直接描いた後の描き直しがこれ)。frameを通さずに液晶へ描くときは
    `InvalidateLcdRows()`。5秒ごとのシリアルの`fps:`の行に`px/frame`(送った画素)と`skipped`(飛ばした画素)を出す。
- **描画中にクリップを外していた**(以前からのバグ。上の2回描きに隠れていた): `Label::DrawPlain()`(`pico.draw_text`等)・
  `IconRender::DrawIconRaw()`・`AppGrid`・`MarkdownView`の表が`setClipRect()`→`clearClipRect()`していたため、
  `FlushDirty()`が掛けた「dirty矩形の内側だけ」のクリップが外れ、その後の描画がdirty矩形の外へはみ出していた
  (送られないのでframeと液晶が食い違い、行を飛ばす最適化と組み合わさると古い絵が残る。テトリスのSCOREの数字で踏んだ)。
  **`gui/ScopedClip.hpp`**(今のクリップとの重なりへ狭め、抜けるときに元へ戻す)に置き換えた。
  `pico.set_draw_area/clear_draw_area`も、描画中(`PICO_GFX::render_clip_active`)は**ウィジェットの描画範囲∩dirty矩形**
  (`render_clip`)の外へ広げない/そこへ戻すようにした。**部品の中で一部だけクリップして描くときは`ScopedClip`を使うこと**(合成の外の空のクリップも保つ。下の「ミュージックアプリ」の「ついでに直したもの」)
  (`clearClipRect()`を描画の途中で呼ばない)。
- `pico.game`: カメラが動いたときの描き直しをワールドの見える範囲だけにした(画面ボタンの帯は描き直さない)。
- **確かめ方(PCビルドだけ)**: `PICOOS_VERIFY_LCD=1`で、`FlushDirty()`のたびに液晶(SDLパネル)の中身をframeと全画素比べ、
  食い違えば`[VERIFY]`と場所をログへ出す(`VerifyLcdMatchesFrame()`)。`PICOOS_NO_ROW_SKIP=1`で行を飛ばす最適化だけを切れる(比較用)。
  わざと「変わった行も飛ばす」ように壊すと食い違いを報告することを確かめてある。ランチャ・入力テスト(キーボードのダイアログ)・
  ペイント・テトリス・時計・ジャンプアクションで食い違い0。
- 結果(PCビルド、SPIは下の37.5MHzを真似る): カメラが流れ続ける場面で**11fps・15万画素/フレーム → 約60fps・1.6万画素/フレーム**。
  **実機では未計測**(Luaの描画・4bpp→RGB565の変換のCPU時間はPCでは見えない)。
- **⚠ `TFT_MAX_SPEED`(60MHz)は実際には37.5MHzで動いている**: LovyanGFX(rp2040)は`clk_peri`(150MHz)を2で割った75MHzを
  さらに整数(1+SCR)で割るので、選べるのは75 / 37.5 / 25MHz…だけで、**37.5〜74.99MHzを指定するとすべて37.5MHz**になる
  (`FreqToClockDiv()`。`pc/compat/config/Panel_sdl_SpiWait.hpp`も同じ計算)。全画面1枚の転送は37.5MHzで約33ms(30fps止まり)、
  75MHz(`80000000`等、75MHz以上を指定)なら約16ms。タッチのノイズの調査で下げたが、ノイズはクロックに依らなかった
  (上の「タッチ座標のノイズ抑制」)。上げるかは実機の安定性次第。

### dirty矩形を固定長配列化(`src/gui/core/GFX_Functions`) (2026-09-28)

パフォーマンス監査で見つかった、`PICO_GFX::dirtyRects`が`std::vector<Rect>`のままだった点への対応。
`FlushDirty()`のたびに`dirtyRects.clear()`→次フレームの`push_back()`で伸縮を繰り返しており、
「確保ゼロ」方針(MarkdownViewのプール等)から外れていた。加えて`reserve(48)`は**シーン遷移直後の
実測で軽く超える**件数で、実質恩恵が無かった。

- **`std::vector<Rect> dirtyRects`を`Rect dirtyRects[kMaxDirtyRects]`(固定長配列)+
  `int dirtyRectCount`へ置き換えた**(`kMaxDirtyRects=128`。48→128へ拡張)。ヒープ確保が
  完全に無くなる。
- **128件を超えて`MarkDirty()`された場合は、個々の矩形を追うのを諦めて画面全体を
  1枚のdirty矩形として扱う**(`dirtyOverflowed`フラグ)。溢れた時点で積んであった
  個別の矩形は(どうせ全画面転送に飲み込まれるので)破棄し、以降の`MarkDirty()`も
  静かに無視する。`FlushDirty()`が呼ばれた瞬間に`dirtyRects[0]`を`{0,0,SCREEN_WIDTH,SCREEN_HEIGHT}`
  へ差し替えて1件だけ処理する(取りこぼしが無く、128枚ぶんの当たり判定・転送より軽い)。
- 検証はPCビルドで実施: 通常の`--shot`/`--tap`で見た目に変化が無いこと、
  `kMaxDirtyRects`を一時的に3まで下げてほぼ毎フレーム溢れさせた状態でも
  クラッシュせず正しく描画されること(画面全体を1枚として扱うので、部分描画時と
  見た目は変わらない)を確認した。ホストテストは無し(`GFX_Functions`は上記
  「画面の明るさ調整と自動調光」と同じ理由でASan対象外)。

### 文書キャッシュ目録(index.tsv)の書き込みをO(n)からほぼO(1)へ (`src/apps/browser/Doc_Cache.cpp`) (2026-09-28)

パフォーマンス監査で見つかった、`PICO_DocCache::SetEntry()`/`RemoveEntry()`が常に
`rewriteIndex()`(目録全体を読みながら一時ファイルへ書き写し、最後にrenameする)を
呼んでいた点への対応。Markdownブラウザで新しい文書を開くたび(=キャッシュに無い
host/pathへ初めて`SetEntry()`する)、その時点までに溜まった目録**全件**を読み直し・
書き直していた。ブラウジングで未訪問のページを開き続けるセッションでは、
1件追加するたびのコストが目録の総件数に比例して伸び続ける(n件目の追加がO(n)、
合計でO(n²))。

- **`Lookup()`の中身を`scanIndexFor()`という読み取り専用の走査へ切り出した**
  (振る舞いは変えていない、単純な抽出)。`SetEntry()`/`RemoveEntry()`からも
  「本当に既存行があるか」を確認するためだけに呼べるようにするための下準備。
- **`SetEntry()`は、`scanIndexFor()`で既存行が無いと確認できたら`appendIndexEntry()`
  (目録の末尾へ1行追記するだけ)で済ませる**。既存行がある(内容が更新された)場合
  だけ、これまで通り`rewriteIndex()`で全体を書き直す。ブラウジングで新しい文書を
  開くたびに通る経路(未訪問ページの初回キャッシュ)が最も軽くなるよう狙った
  (読み取りは相変わらずO(n)だが、**書き込みが「全件読み書き+一時ファイル+rename」
  から「1行の追記」へ縮む**。SDは書き込みの方が読み込みより重いため、体感の差は
  読み取り側の計算量が変わらない以上に大きいはず)。
- **`RemoveEntry()`も同様に、`scanIndexFor()`で対象行が無いと分かれば目録に一切
  触れず即`true`で返す**。`Remove()`(本体を消す口)は本体が無くても目録の掃除を
  試みる作りのため、この経路は実際によく通る。**以前は目録が存在しない状態から
  でも`rewriteIndex()`が空の`index.tsv`を新規に作ってしまっていた**(削除対象が
  見つからなくても、読み書きの手順自体は素通りで最後まで進んでいたため)。今回の
  変更でこの副作用も無くなった。
- **`Doc_Fetch`側の呼び出し元(`DocFetch::begin()`/`update()`)は一切変更していない**。
  `SetEntry()`/`RemoveEntry()`の入口だけで「新規か更新か」を判断しており、
  外から見た振る舞い(目録には1つのhost/pathにつき常に高々1行、という不変条件含む)
  は完全に保たれる。
- 検証は`script/host_test/cache_test.cpp`(既存の全項目に加えて、
  「存在しないエントリの削除は目録に一切触れない(ファイルすら作らない)」
  「新規追加5件それぞれが正しく引ける」を追加)。PCビルドで`script/reference_server.py`を
  相手に実際にMarkdownアプリから文書を開き、`index.tsv`に正しく1行追記されること、
  同じページを再訪問しても(304になり`SetEntry()`が呼ばれないため)目録が
  1バイトも変わらないことを確認した。

### Luaコールバックの探索を線形からid昇順+二分探索へ (`src/lua/LuaEngine`) (2026-09-28)

パフォーマンス監査で見つかった、`LuaEngine::Dispatch()`/`DispatchClosed()`/
`DispatchSelectItem()`が毎回`callbacks_`(**アプリ全体**のコールバック登録の合計)を
先頭から線形探索していた点への対応。1ウィジェットが持てるイベント種別は
`EventKind`の12種までなので「1ウィジェットあたりは十分速い」という当初の判断
(クラスコメント「コールバック中継の設計」参照)は正しいが、**アプリ全体**では
ウィジェット数×イベント数ぶん(テトリス/ペイントのような多ボタンLuaアプリでは
数十〜百程度)に比例して線形探索のコストが伸びる。`render`イベントは毎フレーム
`Dispatch()`されうるため、この経路はホットパスになりうる。

- **`callbacks_`を`WidgetId`の昇順に保つという不変条件を導入した**
  (`BindCallback()`/`PruneCallbacksFor()`が維持する)。新設した
  `FindCallback(id, kind)`が`std::lower_bound()`でidの区間をO(log n)に絞り、
  そこから先はEventKindの種類数(高々12)ぶんだけ線形に見る(実質定数時間)。
  `Dispatch()`/`DispatchClosed()`/`DispatchSelectItem()`は全てこれ1つに寄せた。
- **`BindCallback()`の新規挿入は`std::lower_bound()`で見つけた位置へ`insert()`する**
  (同じidの中での並び順は`FindCallback()`が線形走査するので問わない)。
  `PruneCallbacksFor()`も同様にidの区間を二分探索で絞ってから、その範囲をまとめて
  1回の`erase(first, last)`で消す(以前は1件ずつ`erase(it)`していたため、複数件
  持つウィジェットの破棄はO(件数×n)だった)。
- **`Dispatch()`系は`FindCallback()`が返すポインタから`ref`(int)を即座に値コピー
  してから`ProtectedCall()`(Lua呼び出し)へ入る**。Lua側のコールバックが
  `pico.on()`/`pico.destroy()`を呼ぶと`callbacks_`(`std::vector`)が再確保・移動
  されうるため、ポインタ/参照をLua呼び出しの前後で跨いで持たない(元のコードも
  `ref`を値でコピーしてから使っていたので、その規律をそのまま踏襲しただけ)。
- コンテナ自体は変わらず`std::vector<CallbackBinding>`のまま(確保回数・メモリ
  footprintに変化は無い)。挿入・削除の実行時間計算量自体はvectorの性質上O(n)
  (要素の詰め直しが要る)のままだが、そちらは`pico.on()`/`pico.destroy()`という
  低頻度の操作でしか起きない。**高頻度(毎フレームありうる)な`Dispatch()`系だけを
  O(n)からO(log n)へ落とす**のが狙いで、既存の「ヒープ確保を増やさない」方針
  (クラスコメント参照)とも矛盾しない。
- 検証: `script/host_test/lua_engine_test.cpp`(既存の全項目。共通4イベント+
  `render`+`closed`+ウィジェット固有6種+コンテナ操作+HTTPまで、`callbacks_`が
  絡む経路を一通り踏む)、`script/host_test/lua_scene_test.cpp`をASan/UBSan付きで
  実行し、並び替え後も全件パスすることを確認。PCビルドでは「テトリス」
  (タッチボタン6つ+HOLD/BGM枠のタップ+盤面のCanvas `render`を毎フレーム
  dispatchする、最もコールバック数の多いLuaアプリ)を実際に起動し、ゲーム開始・
  ソフトドロップ操作・スコア加算・盤面の差分描画までタップで操作して確認した。

## メモリ計測の結論 (2026-09-12時点、実機RP2350で20回の遷移を計測)

`src/system/Mem_Functions.hpp` と `script/host_test/run_mem.sh` で実測した結果と判断。
**同じ議論を繰り返さないために、アリーナを提案する前に必ずここを読むこと。**

- **断片化は起きていない**。`frag=0%` / `max_alloc=65536B以上` / 空きブロック数5〜12で推移し、20回の遷移で増加傾向なし。
- **リークも無い**。「ヒープ下限」(シーン破棄直後のused。シーンのウィジェットが1つも生きていない瞬間)は9回時点で+248B、19回時点で+192Bと、回数を倍にしても増えない。
- 当初observedしていた使用量の増加は断片化ではなく解放漏れで、`MarkdownView`/`ScrollList`/`CanvasRaster`のデストラクタ修正で解消済み(Markdownは1訪問あたり約57KBリークしていた)。
- **アリーナの枠を決めるなら根拠は「ウィジェット本体のバイト数」だけ**。レポートの`widget`列がそれ。シーン全体の`peak`には内部の`std::vector`/`std::function`が含まれるが、それらはアリーナではなくグローバルヒープに残るため、枠の根拠にすると3割ほど過大になる。
- 実測値: 永続(Statusbar+キーボード3種)=4,600B / Markdown=41,668B / InputTest=1,552B / Home=756B。
  **プールが全部固定長なので、開く文書が変わっても`Markdown`の41,668Bは動かない(決定論的)**。
  → 入れる場合の推奨枠は「永続8KiB + シーン56KiB = 64KiB」。
- アリーナが捕まえるのは Markdown の全841回の確保のうち**36回(バイトでは79%、回数では4.3%)**だった。残り805回は`Label`の行データ(`vector<vector<TextRun>>`)と`Widget`基底の`std::function`×4で、これはアリーナでは消えない。
  - **※この805回のうち`Label`ぶんはその後の改修で潰した**(行データを`vector<vector<TextRun>>`から
    `runs_flat`+`TextRun::line`へ平坦化し、レイアウトを`needs_relayout`で遅延評価に変更)。
    **現在は `MarkdownView + load()` で88回**(うち本体以外が87回)、**通常のシーン遷移は1回あたり8回**。
    `sh script/host_test/run_mem.sh` で再現できる。**TODOに残る「841回中805回」は既に古い数字**。
  - **残りの実害は「確保回数」ではなく「sizeof」のほう**。`std::function`は32Bで`Widget`基底に4本あるため、
    **1ウィジェットあたり128Bが固定で乗る**(`Button`のsizeof 312Bのうち128B)。関数ポインタ+`void*`の
    Delegate(16B)へ替えれば1個あたり64B減るが、Markdownシーン36個でも約2.3KB/46KB(5%)。
    **単体では旨味が薄い。**
    ~~Luaのコールバック(`lua_State*`+registry refのキャプチャは16B超え=貼るたびにヒープ確保)を
    大量に貼るようになって初めて費用対効果が出る~~ →
    **この前提は実装時に回避できた(2026-09-19、下記「Luaバインディング」参照)**。
    `lua_State*`やregistry refをウィジェット側のstd::functionへ直接キャプチャさせず、
    「`LuaEngine*`(1ポインタ)+`WidgetId`(4B)」だけをキャプチャする中継関数を挟むことで、
    小バッファ最適化の範囲(概ね16B)に収まりヒープ確保が起きないようにした。
    そのためコールバックのDelegate化は今後も「単体では5%程度」の効果しかなく、優先度は低いまま。
- **判断: 断片化もリークも観測されていない以上、64KBを常時占有する対価に見合わないため保留**。アプリが増えて断片化が実際に観測された時点で再検討する。
  → **2026-10-27、常時占有しない形(下記「シーンアリーナ」)で入れた。**

## シーンアリーナ (2026-10-27)

上の「保留」の理由は「64KBを常時取る」ことだったので、**領域を常時は取らない形**で入れた。あわせて、
アリーナでは消えない「ウィジェットの中の確保」(上の805回の残り)と、シーンの途中で作って捨てるダイアログも片付けた。

- **アリーナ本体**(`util/ChunkArena.hpp`、つなぎは `gui/widgets/WidgetArena.hpp` と `Widget.cpp` の `operator new/delete`):
  4KiBの塊を**要るぶんだけヒープから借り**(最大16個)、塊の末尾から切り出す。シーンを離れると
  (`SceneFunctions` の `TeardownCurrent()` → `WidgetArena::OnSceneExit()`)空いた塊を全部ヒープへ返す。
  - 起動時の常駐ウィジェット(ステータスバー・キーボード・トースト・ロック画面・計測の重ね表示)は今までどおりふつうのヒープ。
    `main.cpp` が `SealPermanentBaseline()` の直後に `WidgetArena::Enable(true)` してから先だけがアリーナへ入る。
  - 塊の半分(2KiB)を超える物(`MarkdownView` 17KB、`Label<1KiB>` 等)と、塊が16個に達した後はふつうのヒープへ回す(溢れても動く)。
  - 解放は確保の大きさ(sized delete)から引くので、塊の中に見出しを置かない。末尾なら巻き戻し、そうでなければ大きさごとの空きとして覚え、
    同じ大きさの確保で使い回す。塊の中が全部空いたら塊を返す(いま切り出し中の1個だけは空にして持つ)。
  - 代償: シーンの使う量が塊の単位(4KiB)へ切り上がる。PCの計測で Home +2.2KB → +4.1KB、Clocks +9.0KB → +13.5KB(enter列)。
  - ASan付きのホストテストでは空きと塊の未使用部分に毒を塗る(解放後の読み書きを `widget_arena_test` 等で捕まえる)。
    ホストテストは既定でアリーナを切ってある(`Enable()` を呼ぶのは `main.cpp` だけ)。
- **ウィジェットの中の確保を0にした**:
  - コールバック(`Widget` の `on_press_*` 4本、各ウィジェット・ダイアログの `setOnXxx`)を `std::function` から
    **`util/InplaceFunction.hpp`** へ。キャプチャをポインタ3つぶんの箱に持ち、**収まらなければコンパイルエラー**
    (`std::function` は黙ってヒープへ逃がしていた)。大きさは `std::function` と同じ(32bitで16B)。
    箱を超えていた9か所(配列・`FixedString` の値キャプチャ。ホームのメニュー・エディタの破棄の確認・
    名前の変更・録音の削除・Wi-Fiの削除・読書の「続きから」・ゲームボーイのメニュー)は、値をシーンのメンバへ移してthisだけを持つようにした。
    **実機(32bit)の箱は12Bで、ポインタとintを混ぜたキャプチャはPCで通っても実機で落ちることがある**(ゲームボーイのメニューがそうだった)。
  - 子の一覧(`getChildren()`)を `std::vector<Widget*>` から **`WidgetList` = `SmallVector<Widget*, 8>`**(`util/SmallVector.hpp`)へ。
    8個までは自分の中に持つ。`Label` の行データ(`runs_flat`・`line_offsets`)と `parseMarkup()` の一時の並びも `SmallVector`。
  - `WidgetFunctions::ProcessPendingDeletes()` が毎回 `pending_deletes` を確保し直していた(`std::move` で持ち出していた)のを、2本の領域の入れ替えにした。
- **途中で作って捨てるダイアログの使い回し**: ダイアログ側に「作り直し(reinit)」を足す形はやめ、**アリーナの空きの使い回しで行う**。
  86か所の `new XxxDialog(...)` → `DestroyLater()` をそのままにでき、閉じた後の状態(コールバック・ウィジェットID・フォーカス)を
  消し忘れる心配が無い。閉じたダイアログの部品は(後から作った物が無ければ)末尾の巻き戻しで、あれば大きさごとの空きで、
  次に開いたダイアログが同じ場所を使う。ダイアログの部品はどれも2KiB未満(64bitで `PickerDialog` 424B・`Label<512B>` 1568B 等)なので全部アリーナへ入る。
- 計測(`sh script/host_test/run_mem.sh`、PC):

  | | 前 | 後 |
  |---|---|---|
  | `Label<M>` 短文 / 装飾つき / `Textbox<LL>` の確保回数 | 3 / 9 / 7 | 1 / 1 / 1(本体だけ) |
  | `MarkdownView + load()` の確保回数 | 88 | 37(本体36個+文書の並び1) |
  | シーン遷移1回のヒープ確保(アリーナ有効) | 8 | 塊の貸し借り1回+シーンのオブジェクト1回 |
  | `MsgDialog` 50回の開閉でのヒープ確保 | 毎回(部品5個の本体+子の一覧・行データ・削除待ちの一覧) | 50回で1回(削除待ちの一覧が最初に1回) |
  | 実機ファーム(`rpipico2w`) | RAM 131,544B / Flash 2,223,864B | RAM 131,840B / Flash 2,212,392B(-11.5KB) |

  `sizeof` は `Label` が+72B(64bit。内に持つ並び)、子を持つウィジェットが+32B(32bit。`WidgetList`)ほど増えた。
- **実機では未確認**(シーン遷移を繰り返したときの `[MEM]` の「シーンアリーナ」行・ヒープ下限・断片化率)(Todoist参照)。

### ✅ 解決済み: PCビルドでシーン遷移を繰り返すと「ヒープ下限」が際限なく増える(2026-09-19発見 → 2026-09-28特定・修正)

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

### ✅ 解決済み: 同じ`pc/build`を2回目以降configureすると必ず失敗する(2026-09-28発見・修正)

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
