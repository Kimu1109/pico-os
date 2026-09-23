# pico-os 開発状況

> 最終同期: 2026-09-23(実コードと突き合わせ済み)。
> このファイルは**何が終わって何が残っているか**の一覧。設計の背景や実装の詳細は `CLAUDE.md` を参照。
>
> - **TODO** … 項目名だけの一覧。全体像を掴む用。
> - **詳細** … TODOで補足が要る項目の説明。TODOの見出しからリンクしてある。

## 全体の進捗

| # | 項目 | 状況 |
|---|---|---|
| 1 | [ダイアログ系統](#1-ダイアログ系統) | ✅ 完了(betaレベル) |
| 2 | [汎用基盤](#2-汎用基盤) | ✅ 完了(Resolve()もLua統合から実利用済み) |
| 3 | [スクリーン管理](#3-スクリーン管理) | 🔨 メモリプールは計測の結果いったん保留 |
| 4 | [Wi-Fiの管理強化](#4-wi-fiの管理強化) | ✅ 完了 |
| 5 | [Luaアプリ](#5-luaアプリ) | ✅ LuaEngine/LuaSceneが動作しウィジェット・ダイアログ・SD・画像・ネットワーク・シーン制御・権限管理・実行時間の安全網・SDスキャンによるアプリ自動登録まで実装済み。細部の穴(pico.remove_child/list_add等)も埋まり、既知の欠けは無い |
| 6 | [PC/Web動作対応](#6-pcweb動作対応) | ✅ 完了 |
| 7 | [標準アプリ開発](#7-標準アプリ開発) | ✅ Markdownブラウザ / 時計 / 電卓 / ファイルエクスプローラー / 設定 / 辞書が完了 |
| 8 | [セカンダリアプリ開発](#8-セカンダリアプリ開発) | ⬜ 未完了 |
| 9 | [GameBoyエミュ](#9-gameboyエミュ) | ⬜ 未着手 |
| 10 | [外部コントローラー](#10-外部コントローラー) | ⬜ 未着手 |
| 11 | [Chiptuneを再生](#11-chiptuneを再生) | ⬜ 未着手 |

---

# TODO

### 1. [ダイアログ系統](#1-ダイアログ系統-1)

- [x] キーボード日本語
- [x] キーボード英語
- [x] キーボード数字(電卓用)
- [x] キーボードのカーソル移動
- [x] メッセージダイアログ
- [x] インプットダイアログ
- [x] ファイル保存ダイアログ
- [x] ファイル選択ダイアログ
- [x] 色選択ダイアログ

### 2. [汎用基盤](#2-汎用基盤-1)

- [x] 新規ウィジェットの汎用化
- [x] 固定長文字列クラス
- [x] ウィジェットの固有ID
  - [x] 消費側: ファクトリ(WidgetType→new Xxxの対応表)
  - [x] 消費側: Resolve()の実際の呼び出し元(Lua統合本体から実利用)
- [x] 設定ファイルの書き込み
- [x] アプリの枠組み

### 3. [スクリーン管理](#3-スクリーン管理-1)

- [x] ウィジェットのメモリ解放
- [x] シーン遷移
- [x] 並び方について: パネル、グリッド
- [ ] メモリ断片化対策: メモリプール
  - [x] ヒープ使用量・断片化の計測
  - [x] ウィジェットの解放漏れ修正
  - [x] MarkdownViewの占有量削減
  - [x] アリーナの差し込み口を用意
  - [ ] シーンアリーナ本体
  - [ ] ウィジェット内部のstd::vector / std::functionの確保削減
  - [ ] mid-sceneで生成/破棄されるダイアログの使い回し化
  - [ ] ⚠ PCビルドでのヒープ下限際限増加(未解決)の原因特定・実機での再現確認

### 4. [Wi-Fiの管理強化](#4-wi-fiの管理強化-1)

- [x] 定期的再接続交渉
- [x] 確実な時刻同期

### 5. [Luaアプリ](#5-luaアプリ-1)

- [x] 着手前に塞ぐ穴
  - [x] AppEntryの動的化
  - [x] ウィジェットのファクトリ
  - [x] プロパティのget/set共通口
  - [x] Lua用allocatorでのRAM上限
  - [x] pcallで拾ったエラーの表示導線
  - [x] ビルドの二重管理解消(Lua本体のvendor)
- [x] Luaソースの動作(LuaScene)
- [x] LuaとC++をつなぐAPIの設計(pico.* API)
- [x] APIの実装、検証
  - [x] ウィジェットの生成/破棄/プロパティ/共通コールバック
  - [x] setup()/loop(dt)呼び出し
  - [x] 直接描画(Canvas)
  - [x] SDカードアクセス
  - [x] 画像(.pimg)
  - [x] シーン制御(push_scene/change_scene/launch_app)
  - [x] ダイアログ
  - [x] ネットワーク(HTTPリクエスト)
  - [x] 権限管理(ネットワーク/app_dir外SDアクセス、粗いフラグ)
  - [x] ウィジェット固有コールバック(Checkbox/NumberSlider/ScrollList/TabBar/DropdownMenu/Textbox)
  - [x] 時刻取得(pico.get_time())
  - [x] コンテナからの子の取り外し(pico.remove_child)
  - [x] リストへの項目追加(pico.list_add/list_clear/pico.tab_add)
- [ ] 残タスク
  - [x] 命令単位の実行時間制御(lua_sethook等)
  - [x] SDを走査してLuaアプリを見つける処理
  - [ ] 実機の空きRAM/Flashの実測

### 6. [PC/Web動作対応](#6-pcweb動作対応-1)

- [x] LovyanGFX対応
- [x] タッチ操作対応
- [x] ネイティブ関数の代替関数
- [x] ブラウザ動作対応(WebAssembly)
- [x] Webビルドの自動公開
- [ ] その他あれば

### 7. [標準アプリ開発](#7-標準アプリ開発-1)

- [x] 設定アプリ
- [x] 時計
- [x] 辞書
- [x] 電卓
- [x] ファイルエクスプローラー
- [x] Markdownブラウザ
  - [x] リンク配線 + 履歴 + ナビゲーションヘッダー
  - [x] キャッシュ層
  - [x] HTTPクライアント + 参照実装サーバ
  - [x] 取得→キャッシュ→表示の配線
  - [x] 画像の解決と先読み
  - [x] discovery(サーバ情報)
  - [x] 遷移に失敗したときの巻き戻し
  - [x] リロードボタン
  - [x] 検索 + 結果画面
  - [x] マニフェストでキャッシュを一括再検証
  - [x] HTTPS(TLS 1.2)とchunked転送

### 8. [セカンダリアプリ開発](#8-11-未着手の大項目)

- [ ] ペイント
- [ ] スクラッチパッド
- [ ] チャットツール
- [x] マインスイーパー
- [x] オセロ風
- [ ] テトリス風
- [ ] PICO SHOOTING
- [x] ブロック崩し風
- [x] カレンダー
  - [x] .icsの読み取り(繰り返し/例外)
  - [x] 月表示の画面
  - [x] iCal URLから取得(HTTPS)
  - [x] 予定の詳細画面(説明文)
  - [x] カレンダーごとの色分け

### 9. [GameBoyエミュ](#8-11-未着手の大項目)

- [ ] 最適なGBエミュを探せ
- [ ] GBエミュを適合させよ
- [ ] 快適動作を目指そう

### 10. [外部コントローラー](#8-11-未着手の大項目)

- [ ] 配線/方式を考えよう
- [ ] 入力を受け取ろう
- [ ] 実際にアプリに組み込めるようにしよう

### 11. [Chiptuneを再生](#8-11-未着手の大項目)

- [ ] とりあえず再生
- [ ] GB対応
- [ ] 標準ファイル形式を探す/考える
- [ ] アプリ対応

---

# 詳細

以下はTODOの補足。**書いていない項目は「名前のとおり」で補足なし。**

## 1. ダイアログ系統

ダイアログの共通の骨格(`children_`で子を保持 / `setOnClosed()`で結果通知 / `setVisible(false)`で終了)は
`CLAUDE.md`の「ダイアログ」を参照。

| 項目 | メモ |
|---|---|
| キーボード日本語 | フリック入力 + SKK変換。`カナ`/`送り`キーは変換中でないときカーソル移動キーになる |
| キーボード英語 | 最下段に`←``→`。幅の都合でラベルを `かな` / `enter` / `abc` に短縮し、シフト中も表記を変えない |
| キーボード数字 | `KeyboardNum`。`Digit`/`Arith`/`Math`の3タブで、使えるタブを呼び出し側から制限できる |
| キーボードのカーソル移動 | 数字に続き日本語/英語も、挿入・削除がカーソル位置基準になった。位置は`Label::setCursorToByteOffset()`でバイト位置として渡す |

## 2. 汎用基盤

### ウィジェットの固有ID

`WidgetID.hpp` / `WidgetRegistry`。32bitで `type(6bit) / generation(16bit) / index(10bit)`。

- **発行側は実装済み**。`getId()`の初回呼び出しで遅延発行し、`~Widget()`でgenerationを進める。
- **ファクトリを追加**(`WidgetFactory.hpp/.cpp`)。`WidgetType` → `new Xxx` の対応表で、
  widgets/直下の汎用部品15種(Button/Label/Textbox/NumberInput/Checkbox/Icon/Image/
  NumberSlider/ScrollContainer/ScrollList/CanvasRaster/LayoutContainer/GridContainer/
  TabBar/DropdownMenu)を生成できる。widgets/apps・systems・dialogsの専用ウィジェットは
  対象外(SD走査やシーン固有状態への依存が強いため)。
- **`Resolve()`はホストテスト(`widget_factory_test.cpp`)で発行済みIDからの解決・
  type不一致の検出・破棄済みID(use-after-free)の検出・スロット再利用時のgeneration
  不一致検出を確認済み**。**その後`LuaEngine`(`pico.set/get/on/destroy/add_child`等)
  から実際に呼ばれるようになり、実コードでの利用も確立した**。
- Widget::operator newがnullptr(確保失敗)を返した際、以前は無言で失敗していたが、
  唯一の確保入口である`Widget.cpp`側でLOG_SYS_FAILを出すようにした。
  個々の`new Xxx(...)`呼び出し元のnullチェックが無い問題自体は残っている
  (Lua用allocatorの話と合わせて[5. Luaアプリ](#5-luaアプリ-1)を参照)。
- 詳細は[5. Luaアプリ](#5-luaアプリ-1)。

## 3. スクリーン管理

### メモリ断片化対策: メモリプール(保留)

**計測の結果、現時点では保留と判断した。** 根拠の全文は`CLAUDE.md`の「メモリ計測の結論」。
アリーナを再提案する前に必ずそちらを読むこと。

- 断片化は起きていない(`frag=0%`、20回の遷移で増加傾向なし)。リークも無い。
- 当初の使用量増加は断片化ではなく解放漏れで、`MarkdownView`/`ScrollList`/`CanvasRaster`の
  デストラクタ修正で解消済み(Markdownは1訪問あたり約57KBリークしていた)。

| 子項目 | 状況 |
|---|---|
| 計測 | ✅ `src/functions/Mem_Functions.hpp` / `script/host_test/run_mem.sh` |
| 解放漏れ修正 | ✅ `MarkdownView` / `ScrollList` / `CanvasRaster` |
| MarkdownViewの占有量削減 | ✅ 実機で93KB → 52KB |
| アリーナの差し込み口 | ✅ `Widget::operator new/delete`。将来ここの実装を差し替えるだけで済む |
| シーンアリーナ本体 | ⏸ 断片化もリークも観測されないため保留。入れる場合の枠は「永続8KiB + シーン56KiB = 64KiB」(実測根拠あり) |
| vector/functionの確保削減 | ⏸ **「841回中805回」は既に古い数字**。Labelの平坦化後は `MarkdownView + load()` で88回、通常のシーン遷移は1回あたり8回。残る実害は回数ではなく`sizeof`(`std::function`が32B×4本=1ウィジェット128B)で、Delegate化してもMarkdownシーン36個で約2.3KB/46KB。**Luaのコールバックを大量に貼るようになって初めて効く** |
| ダイアログの使い回し化 | ⏸ 現状の生成箇所は`FileExplorer`の2箇所のみ。効果が測れる規模になってから |
| PCビルドでのヒープ下限際限増加 | ⚠ **未解決(2026-09-19発見)**。実機の計測では増加傾向が無かったが、PCビルド(`pc/build/picoos_pc`)で同一2画面を`--tap`で機械的に往復させると「ヒープ下限」が回数に比例して際限なく増える(1往復あたり約30〜40KB)。**Luaとは無関係**(`InputTestScene`だけで再現し、Lua関連の変更を含まないコミットでも同じ数値が出た)。原因未特定(候補: LovyanGFXのフォント/グリフキャッシュ、Task/Network_Functions、SDLのイベント処理)。再現手順は`CLAUDE.md`の該当節を参照。次にScene/Widget基盤・PCビルド周りを触る回で必ず引き継ぐこと |

## 4. Wi-Fiの管理強化

非ブロッキング接続・スキャン(Task化)・NTP同期・電波強度アイコンに加え、
`SUCCESS`中は`HEALTH_CHECK_INTERVAL=5000ms`ごとに`WiFi.status()`を確認し、
切断を検知したら`ConnectWiFiAsync()`を呼び直す(`currentPassword`を再接続用に保持)。

## 5. Luaアプリ

`src/lua/LuaEngine`(1インスタンス=1つの`lua_State`=1つのLuaアプリ)と`LuaScene`
(SD上のスクリプトを読んで実行する画面)が動き、**ランチャから実際にLuaアプリを
起動できる**(`pc/sdcard/lua/hello.lua`、ランチャに「Lua Hello」タイルあり)。
設計の背景・各APIの詳細は`CLAUDE.md`の「Luaバインディング」を参照。

| 穴/項目 | 状況 |
|---|---|
| AppEntryの動的化 | ✅ `create`が`Scene* (*)(const AppEntry&)`になり、`name`/`arg`を`FixedString`でコピー保持する。同じシーン型を別argで何件でも登録でき、寿命の短い文字列からも登録できる(`MakeSceneWithArg<T>`) |
| ウィジェットのファクトリ | ✅ `WidgetFactory::Create(WidgetType)`。`Resolve()`と合わせて`pico.create/get/set/on`から実際に使われている |
| プロパティのget/set共通口 | ✅ `WidgetProperty::Get/Set()`。`pico.set/get`から実際に叩かれる |
| Lua用allocatorでのRAM上限 | ✅ `lua_newstate`へ`budget_bytes`付きカスタムallocを渡す。暫定枠200KB(`LuaScene::kLuaBudgetBytes`) |
| pcallで拾ったエラーの表示導線 | ✅ `ErrorFunctions::ShowFatal()`。構文/実行時エラーをログ+MsgDialogの両方で見せる |
| ビルドの二重管理 | ✅ Lua 5.4.7本体を`lib/lua/`へvendor。`platformio.ini`への追記は不要になり、PCビルドも同じ`lib/lua/src/`を参照する実質1箇所の情報源に |
| 実行時間バジェット | ✅ `task/StepBudget.hpp`(時間で区切る土台)に加え、`LuaEngine`が`lua_sethook(LUA_MASKCOUNT)`でLuaバイトコード命令数を数え、1回の外部呼び出しあたりの上限(暫定200万命令)を超えたら`luaL_error()`で打ち切る。終わらないループを含むLuaコールバックでOS全体が固まる事故を防ぐ(既知の限界: Lua側の`pcall`で握り潰して再試行し続ける敵対的スクリプトまでは防げない) |
| SDを走査してLuaアプリを見つける処理 | ✅ `src/lua/LuaAppScanner`。`/lua/apps/<名前>/main.lua`を走査し、ディレクトリ名をそのままタイル名として`AppFunctions::Register()`する。権限は既定(両方false)固定。`App_List.cpp::Setup()`の末尾で1回呼ぶだけ |
| ウィジェット固有コールバック | ✅ `checked_changed`(Checkbox)/`value_changed`(NumberSlider)/`select_item`(ScrollList)/`tab_changed`(TabBar)/`dropdown_changed`(DropdownMenu)/`text_changed`(Textbox)を`pico.on()`から追加。値自体は既存の`pico.get()`(プロパティ共通口)で読む設計にし、`select_item`の`already_selected`(永続プロパティではない一時値)だけコールバック引数で渡す |
| 時刻取得 | ✅ `pico.get_time()`。`TimeFunctions::timeinfo`を`{year,month,day,hour,min,sec,wday}`のテーブルで返す薄いラッパー |
| コンテナからの取り外し/リストへの項目追加 | ✅ `pico.remove_child()`(add_childの逆、破棄せず取り外す)/`pico.list_add()`・`pico.list_clear()`(ScrollList/DropdownMenu)/`pico.tab_add()`(TabBar)。細部の穴埋めとして2026-09-21追加 |
| 実機の空きRAM/Flashの実測 | ⬜ 200KB枠はPC上の見積もり(空stateのみで約19.5KB)からの逆算。実機RP2350での追試は未実施(このリモート実行環境にRP2350のボード定義が無いため) |

**実装済みのAPI**: ウィジェットの生成/破棄/プロパティ/共通コールバック・ウィジェット固有
コールバック(`checked_changed`/`value_changed`/`select_item`/`tab_changed`/
`dropdown_changed`/`text_changed`)・コンテナ操作(`add_child`/`remove_child`)・
リスト操作(`list_add`/`list_clear`/`tab_add`)・`setup()`/`loop(dt)`呼び出し・
直接描画(`Canvas`)・SDカードアクセス・画像(`.pimg`)・シーン制御
(`push_scene`/`change_scene`/`launch_app`)・ダイアログ(`show_message`/`show_input`/
`show_file_save`/`show_file_select`/`show_color`)・ネットワーク(`http_request`/`http_cancel`)・
時刻取得(`get_time`)・**権限管理**(`network`/`sd_outside_app_dir`の2値フラグ、既定はどちらも
拒否。`LuaScene`がスクリプト自身のディレクトリを基準に`pico.sd_*`/`pico.image_load`を閉じ込める)。
`pico.*`以外の周辺機能として、**実行時間の安全網**(`lua_sethook`による命令数打ち切り。
終わらないループでOSが固まる事故を防ぐ)と、**SDを走査したLuaアプリの自動登録**
(`LuaAppScanner`。`/lua/apps/<名前>/main.lua`を見つけてランチャへ自動でタイル登録する)も実装済み。

**API仕様は「C++で標準アプリを1〜2本書いてみて、必要になったもの」から逆算した。**
→ [7. 標準アプリ開発](#7-標準アプリ開発-1)

## 6. PC/Web動作対応

実機に書き込まずに、PC上のウィンドウでもブラウザでもpico-osを動かせる(`pc/`、CMake + SDL2)。
**`src/`は実機と同一のまま**で、差し替えているのは実機ライブラリ(`pc/compat/`)だけ。手順は`pc/README.md`。

| 子項目 | メモ |
|---|---|
| LovyanGFX対応 | `lgfx::Panel_sdl`。既定で2倍表示 |
| タッチ操作対応 | SDLのマウスをタッチとして読む。ヘッドレスでは`--tap`で操作を進めて`--shot`で撮る |
| ネイティブ関数の代替 | `pc/compat/`(Arduino/SPI/WiFi/SdFat/タッチ)。代替の用意漏れはリンクエラーで気づける |
| WebAssembly | `emcmake cmake -S pc -B pc/build-web`。`emscripten_set_main_loop`で1フレームずつ回す。SDカードは`index.data`へ同梱、設定はURLのクエリ(`?wifi=disconnected`) |
| 自動公開 | `main`へのpushで https://kimu1109.github.io/pico-os/ へ(`.github/workflows/web-pages.yml`)。プルリクではビルド確認のみ |

**Markdownブラウザのオンライン機能はWebでは動かない**(ブラウザに生TCPソケットが無い)。
Web公開版で試せるのはSD上のローカル文書まで。

## 7. 標準アプリ開発

アプリ本体は6本。
**Lua APIの仕様を逆算するための実例が足りていない**のが本質的な課題。

| アプリ | 状況 / 使える部品 |
|---|---|
| Markdownブラウザ | ✅ 下記 |
| 時計 | ✅ 下記 |
| 電卓 | ✅ 下記 |
| 設定アプリ | ✅ 下記 |
| 辞書 | ✅ 下記 |
| ファイルエクスプローラー | ✅ 下記 |

### 時計(`ClocksScene`)

画面下部の`TabBar`で「時計 / タイマー / ストップウォッチ」を切り替える。

- **時計**: デジタル(`Label`)とアナログ(`AnalogClock`ウィジェット)を上部のタブで切り替え。
- **タイマー**: `DurationPicker`ウィジェット(▲▼で時/分/秒、長押しで連続加算)で設定する。
  鳴ったら数字が赤で点滅する(音が出せないため)。**別のタブを見ていても時間は進み**、
  鳴った時点でタイマーのタブへ引き戻す。
- **ストップウォッチ**: 1/100秒まで表示。書き換えは50ms間隔へ間引く。
- **計測はいずれも`millis()`の差分で積む** — `TimeFunctions`は333msごとの更新な上に、
  NTP同期で時刻が飛ぶため計測には使えない。
- ⚠ ランチャのアイコンが`IconID::AlertTriangle`のまま(時計のアイコンがまだ無い)。
  足すなら`script/tabler_icons/`へSVGを置いて`script/generate_icons.py`を回す。

### 電卓(`CalculatorScene`)

上部の`TabBar`で「電卓 / 履歴」ページを切り替える1画面のアプリ。四則演算+丸括弧+√(平方根)+
π(円周率)+計算履歴に対応。

- **式の評価**は`util/Calc_Eval.hpp`(ヒープ非使用の再帰下降パーサ)が担当。ホストテスト
  (`calc_eval_test`)で四則演算の優先順位・括弧の入れ子・√/π・0除算等のエラーを固定してある。
- **キーパッド**(`gui/widgets/apps/CalculatorKeypad`)は`AppGrid`/`ColorDialog`/`KeyboardNum`と
  同じ「キーごとにButtonをnewせず、固定長テーブル+タップ位置からの逆算」方式。6行4列で
  「0」だけ3マス幅、「=」は右下に置いて反転表示で目立たせる。
- **履歴**は`ScrollList`で表示(最大15件、新しい順)。**2回タップで開く**流儀(`FileExplorer`等と
  同じ): 1回目は選択、選択済みの項目をもう一度タップすると式を電卓ページへ読み戻す。
- **「=」の直後**に演算子を押すと直前の結果から続けて計算し、数字/(/√/πを押すと新しい式として
  上書きする(一般的な電卓の挙動)。
- ホストテスト(`calculator_test`)がキーパッドの「描画位置=タップ判定位置」の整合と、
  入力→式/結果表示→履歴→読み戻しの一連の配線を実際のタッチ経路で検証する。

### ファイルエクスプローラー(`FileExplorerScene`)

「戻る」ボタンを足して`FileExplorer`ウィジェットを1個生成するだけの薄い皮(ClocksScene/
CalculatorSceneと同じ形)。一覧・フォルダの作成/削除・親フォルダへ戻る操作は元々`FileExplorer`
ウィジェット自身が持っているので、シーン側に足したロジックは無い。

- ディレクトリ走査(`FsFile::openNext`)を伴うため、**ホストテストのSdFatスタブでは検証できない**
  (`Doc_Cache::Clear()`と同じ理由: スタブはパス→内容のフラットな`map`でディレクトリの実体が無い)。
  動作確認はPCビルド(`pc/`)の`--tap`+`--shot`で行った: ランチャから起動 → SDカードルート
  (`README.md`/`sys`/`tmp`)の一覧表示 → `tmp`フォルダへ移動 → `←`で親へ戻る →
  シーンの「戻る」でランチャへ復帰、までを実際のタッチ経路で確認済み。

### 辞書(`DictScene`)

「入力欄+検索ボタン+状態表示+結果一覧+詳細欄」を並べるだけの薄い皮
(ClocksScene/FileExplorerSceneと同じ形)。検索ロジック本体は`WordDictionary`
(`src/dict/`)が持つ。

- **前方一致は`search()`が同期的に即返し、語の途中の一致は`update()`を毎フレーム呼んで
  少しずつ拾う**(26MB級の辞書ファイルを毎回全部読むと実機のSD帯域では数十秒かかり
  得るため)。`onUpdate()`で`dict_.update()`を回し、`count()`の増分だけを結果一覧へ
  追記していくので、前方一致だけで足りる大半の検索は一覧がすぐ埋まり、それ以外は
  埋まっていく過程がそのまま見える(検索を止めて待たせない)。
- 結果一覧(`ScrollList`)をタップすると、その場で下の詳細欄へ表示用語句と説明/訳の
  全文を出す。一覧・詳細を別シーン/別ダイアログへ分けなかったのは、辞書を引く操作は
  「一覧を見ながら次を引く」往復が多く、都度画面遷移するとかえって使いにくいため。
- **このシーンのオブジェクトはMarkdownSceneと同じく「数十バイト」の他シーンより大きい**
  (`WordDictionary`が実測約43KB)。辞書アプリは自分の上へ別シーンをPushしないので、
  シーンスタックに積まれたまま残り続ける実害は無い。
- 詳細欄は`detail_title`(表示用語句、太字見出し、スクロールしない固定行)と
  `detail_label`(説明文、`ScrollContainer`で包んでスクロール可能)の2枚のLabelに
  分けてある。1枚にまとめず分けた理由は2つ:
  - 長い説明をスクロールしている間も見出しが常に見えている方が「今何を見ているか」が
    分かりやすい
  - **説明文(3列目)はLabelのマークアップ記号(`**`太字/`_`下線/`~`波線/`~~`取り消し線)を
    普通の文字として使っている**(実データで836行該当。例:「《the~》」「…でも~でもある」)。
    `detail_label`は`setDisableAutoTextDecoration(true)`でマークアップ解釈そのものを
    止め、常に生テキストとして表示する(利用者から「`~`が入ると表示が崩れる」との報告で
    発覚)。表示用語句(2列目)は実データにマークアップ記号を含まないため、
    `detail_title`側は太字装飾を有効なままにしている。
- `detail_label`は`ScrollContainer`(それまで未使用だった汎用部品の初めての実利用)で
  包んである。以前はLabelの`maxHeight`で高さを決め打ちして溢れた分を切り詰めていたが、
  長い説明の途中で見えなくなる不具合(利用者から報告あり)につながっていた。
  `detail_label`側は`maxHeight`を持たず、必要な行数ぶんそのまま伸びる。実利用に伴い
  `ScrollContainer`へ2つ足した: `refreshContentBounds()`(子の大きさが変わった後に
  スクロール範囲を引き直す。追加時とドラッグ開始時にしか再計算しない元の実装のままだと
  中身を書き換えるだけの呼び出し元はスクロール範囲が古いままになる)と
  `scrollToTop()`(表示内容が別物に変わった時に先頭へ戻す)。
- **辞書データ(`script/en-ja-and-ja-en.tsv`)はフォントが描画できない文字(豆腐)を
  含む行を除いてある。** `script/filter_dict_tofu.py`が`script/dict_tofu_chars.txt`
  (この辞書専用の豆腐文字リスト。**`script/tofu-chars.txt`とは別物** —
  そちらはSKK辞書(`skk_body.tsv`、MLサイズ)向けに`checkFontCoverage()`を
  かけた結果で、対象辞書が違うため流用すると豆腐を網羅できない。実際、最初
  `tofu-chars.txt`(162文字)で試みたところ1,827行しか落とせず、この辞書自身を
  `checkFontCoverage()`にかけ直すと未対応文字は2,086文字・10,572行あった)を使い、
  検索用語句・表示用語句・説明のどこか1文字でも豆腐化する行を丸ごと落とす
  (SKK側の変換(`convert_skk_dict.py --exclude-chars-file`)は候補単位で間引いて
  読みは残すが、こちらは1行=1エントリで部分的に伏せ字にする方法が無いため行ごと落とす)。
  行を間引くだけなので検索用語句のバイト順ソートは崩れず、`build_dict_index.py`側の
  再ソートは不要。`dict_tofu_chars.txt`自体の作り方(PCビルドで`checkFontCoverage()`を
  一時的に呼ぶ手順)は`filter_dict_tofu.py`のモジュールdocstringを参照。辞書を
  更新するたびに
  `python3 script/filter_dict_tofu.py <素の辞書> script/dict_tofu_chars.txt --out script/en-ja-and-ja-en.tsv`
  → `python3 script/build_dict_index.py script/en-ja-and-ja-en.tsv` の順で通すこと。

### 設定(`SettingsScene`)

`network.cfg`(Wi-Fi/NTP/ブラウザのホーム/タイムゾーン)と`user.cfg`(起動時セルフチェック)を
1画面のフォームで編集する。**`Config_Functions::SetValue()`・`Checkbox`・`DropdownMenu`の
初めての実利用箇所**(いずれもこれまで発行側/部品のみで、呼び出し元が無かった)。

- **保存ボタンを持たない**。行ごとの操作(編集ダイアログの決定/チェックボックスのタップ/
  タイムゾーンの選択)のたびに`SetValue()`でその場で書き込む。
- テキスト項目(SSID/パスワード/NTPサーバー1・2/ブラウザのホーム)は`MarkdownScene`の検索入力と
  同じ形: 「編集」を押すたびに`InputDialog`を1個newし、閉じたら`DestroyLater()`で破棄する
  (項目ごとに専用ダイアログを持たない)。
- **パスワードは平文を保持しない**。表示は「設定済み/未設定」のみで、編集ダイアログも空欄から
  始まり、空欄のまま決定すると変更しない(既存の値を消せない)。SSID/パスワードを書き換えた
  直後は、もう一方が分かっていれば`NetworkFunctions::ConnectWiFiAsync()`をその場で呼び直し、
  再起動なしで新しい設定へ繋ぎ直す(分からなければ次に両方揃った時点で繋がる)。
- **タイムゾーンは`DropdownMenu`のプリセット選択**(`JST-9`等のPOSIX TZ文字列そのものを項目名に
  使う)。設定ファイルの値がプリセットのどれとも一致しない場合は、その値自体を末尾に追加して
  選択する(独自設定を黒く上書きしないため)。`SettingsScene`実装当時の`DropdownMenu`は選択変更を
  通知するコールバックを持たなかったため、`onUpdate()`で`getSelectedIndex()`を毎フレーム見て
  変化を検出している(`ClocksScene`の`before_sec`等と同じ「変化検出」の流儀)。
  ※`DropdownMenu`自体は2026-09-21のLua API穴埋めで`setOnChanged()`
  (Lua側`dropdown_changed`イベント)を持つようになったが、`SettingsScene.cpp`はこの
  ポーリング実装のまま未移行(上記「5. Luaアプリ」の`dropdown_changed`参照)。
- ブラウザのホームは空欄も許可する(空にすると`MarkdownScene`が同梱サンプル文書を開く)。
  他のテキスト項目は空欄のまま決定しても変更しない。
- 実装に伴い部品側の欠けを2つ埋めた: `Checkbox::causeOnPressStart()`がタップ後に
  `causeOnChangeChecked()`を呼んでいなかった(コールバックを設定しても発火しない)のを配線し、
  `DropdownMenu::setSelectedIndex()`が表示用ラベルを更新していなかった(選択自体は効くが、
  閉じた時の表示がプレースホルダのまま残る)のを直した。いずれも今回が初の実利用で発覚した。

### Markdownブラウザ

「mdのウェブブラウザ」にする方針。通信仕様は`PROTOCOL.md`、参照実装サーバは`script/reference_server.py`。
**PROTOCOL.md の v1 はこれで一通り実装できている。**

| 段 | 内容 |
|---|---|
| リンク配線 + 履歴 + ヘッダー | `MarkdownScene`が履歴(8件、パス+スクロール位置)を自前で持ち、`PICO_IO::resolve()`で相対リンクを解決して同じシーンのまま開き直す。ヘッダに`[<][>][ホーム][更新][検索]`…`[終了]`、フッタに現在のパス(エラー時はメッセージ)。**リンクごとに`SceneFunctions::Push`してはいけない**(スタック上限が4で詰む) |
| キャッシュ層 | `storage/Doc_Cache`。`/cache/<ホスト>/<パス>`へミラーし、目録は`/cache/index.tsv`。`Writer`が一時ファイル(`.part`)→renameで差し替えるので、通信が途中で切れた半端なファイルが残らない。1件64KiBで頭打ち。追い出しは作らない(全消去の`Clear()`だけ) |
| HTTPクライアント | `util/Url`(URL分解・相対解決) / `net/Http_Response`(ソケット非依存の増分パーサ、chunkedは解く) / `net/Http_Transport`(http/httpsの接続。実機はBearSSL、PCはOpenSSL、TLS 1.2・焼き込みのルート証明書+SDの`/sys/tls/ca.pem`) / `task/Http_Get`(`Task`派生、リダイレクト3回・10秒打ち切り・条件付きGET・3xx/4xxの本文はシンクへ流さない)。`run_net.sh`が参照実装サーバとテスト用TLSサーバ相手に実通信で検証する |
| 取得→キャッシュ→表示 | `net/Doc_Fetch`。条件付きGET→304ならキャッシュ据え置き / 200なら差し替え、取得失敗時は古いキャッシュで代用(オフライン表示)。`network.cfg`の`browser-home`でホームを指定できる |
| 画像の解決と先読み | 文書基準でパスを解決(`MarkdownView::resolveRef`、**レイアウトと表示の2箇所**)。リモート文書では表示前に1フレーム1枚ずつ取得し、1枚失敗したら残りは諦める。1ページ8枚まで、キャッシュ済みは取りに行かない |
| discovery | `/.well-known/pico-os`。**ただの文書として取る**ので条件付きGETもオフライン時の据え置きもそのまま効く。知らないキーは無視(前方互換)、`search`の行が無ければ検索非対応、404は「素の静的サーバ」として確定扱い |
| 遷移失敗の巻き戻し | 履歴の現在地は**相対リンクの基準**でもあるので、開けなかった場所を現在地のまま残すと、画面には前の文書が出ているのに次に踏んだリンクだけがその場所を基準に解決される。失敗したら`abortNavigation()`で表示中の位置まで戻す |
| リロードボタン | ヘッダの「更新」。検証子を送らないので304ではなく200が返り、キャッシュごと差し替わる。**挿絵も引き直す**。履歴は積まずスクロール位置も保つ |
| 検索 | ヘッダの「検索」→`InputDialog`で語を聞き、`SearchDialog`(`ScrollList`)に結果を出す(2回タップで開く)。**検索だけは`Doc_Cache`を通さない**(キャッシュのキーがクエリを見ないため、通すと検索語違いの結果が同じファイルへ重なる)。10件ずつで「次へ」あり |
| マニフェスト | `/v1/manifest`。discoveryの直後に1回引き、手元の検証子とversionが一致する文書は**サーバへ何も聞かずに**開く(304の往復も無し)。一致しない/載っていない/非対応サーバなら今までどおり条件付きGETへ落ちる。「更新」を押すとマニフェストも引き直す |

## 8-11. 未着手の大項目

いずれも**土台から設計する前提**。既存機能の拡張ではないので、相談は設計の段から始まる。

| # | 項目 | 現状 |
|---|---|---|
| 8 | セカンダリアプリ開発 | 部品は存在するが、アプリ本体のコードは無い。[7](#7-標準アプリ開発-1)が一巡してから。カレンダーは`.ics`の読み取り(`src/calendar/Ical`)・月表示(`CalendarScene`、SDの`/calendar/*.ics`を読む)・取得元URLからの取得(`Calendar_Sync`、`/calendar/sources.cfg`、HTTPS対応)まで入った(Google CalendarはOAuthではなく非公開のiCal URLで読む方針。`CLAUDE.md`参照) |
| 9 | GameBoyエミュ | コードなし |
| 10 | 外部コントローラー | GPIO/UART連携のコードは無く、入力はタッチのみ |
| 11 | Chiptuneを再生 | 音声出力・PWM/I2S関連のコードは無い |
