# TODO

> 最終同期: 2026-09-13(実コードと突き合わせ済み)。詳細な現状は CLAUDE.md を参照。

- [x] ダイアログ系統
  - [x] キーボード日本語
  - [x] キーボード英語
  - [x] キーボード数字（電卓用） (KeyboardNum。Digit/Arith/Mathの3タブ、使えるタブを呼び出し側で制限可)
  - [x] メッセージダイアログ
  - [x] インプットダイアログ
  - [x] ファイル保存ダイアログ
  - [x] ファイル選択ダイアログ
  - [x] 色選択ダイアログ
- [x] 汎用
  - [x] 新規ウィジェットの汎用化
  - [x] 固定長文字列クラス
  - [x] ウィジェットの固有ID (WidgetID.hpp / WidgetRegistry。32bit: type6/generation16/index10)
    - [ ] 消費側 … Resolve()の呼び出し元とホストテストがまだ無い。WidgetType→実体のファクトリも未整備
  - [x] 設定ファイルの書き込み (PICO_Config::SetValue)
  - [x] アプリの枠組み (登録簿 AppFunctions + ランチャ AppGrid)
- [ ] スクリーン管理
  - [x] ウィジェットのメモリ解放
  - [x] シーン遷移
  - [x] 並び方について: パネル、グリッド (LayoutContainer / GridContainer。Luaから子を動的に積む前提)
  - [ ] メモリ断片化対策: メモリプール(計測の結果いったん保留。詳細はCLAUDE.md「メモリ計測の結論」)
    - [x] ヒープ使用量・断片化の計測 (src/functions/Mem_Functions.hpp, script/host_test/run_mem.sh)
    - [x] ウィジェットの解放漏れ修正 (MarkdownView / ScrollList / CanvasRaster)
    - [x] MarkdownViewの占有量削減 (実機で93KB -> 52KB)
    - [x] アリーナの差し込み口を用意 (Widget::operator new/delete)
    - [ ] シーンアリーナ本体 … 断片化(frag=0%)もリークも観測されないため保留。
          入れる場合の枠は「永続8KiB + シーン56KiB = 64KiB」(実測根拠あり)
    - [ ] ウィジェット内部のstd::vector / std::functionの確保削減
          … 「841回中805回」は既に古い。Labelの平坦化後は MarkdownView+load() で88回、
          通常のシーン遷移は1回あたり8回まで落ちている(run_mem.shで再現可)。
          残る実害は回数ではなくsizeof(std::functionが32B×4本=1ウィジェット128B)。
          Delegate化しても Markdownシーン36個で約2.3KB/46KB。
          → 単体では旨味が薄く、Luaのコールバックを貼るようになってから効く
    - [ ] mid-sceneで生成/破棄されるダイアログ(MsgDialog/InputDialog)の使い回し化
          … 現状の生成箇所はFileExplorerの2箇所のみ。効果が測れる規模になってから
- [x] Wi-Fiの管理強化
  - [x] 定期的再接続交渉
  - [x] 確実な時刻同期
- [ ] Luaアプリ
  - [ ] 着手前に塞ぐ穴 (詳細は CLAUDE.md「Lua着手前の受け皿の状態」)
    - [x] AppEntryの動的化 … create()が AppEntry& を受け取り、name/argをFixedStringで
          コピー保持するようになった。同じシーン型を別argで複数登録でき、寿命の短い
          文字列からも登録できる (MakeSceneWithArg<T>)
    - [ ] SDを走査してLuaアプリを見つける処理 (登録簿側の受け入れ準備は上記で完了)
    - [ ] ウィジェットのファクトリ (WidgetType → new Xxx) とプロパティのget/set共通口
    - [ ] Lua用allocatorでのRAM上限 … Widget::operator newのnullptrを誰もチェックしていない
    - [ ] 実行時間バジェット (lua_sethook or Taskへ載せてコルーチン化)
    - [ ] pcallで拾ったエラーの表示導線 (今はLOG_SYS_FAIL止まり)
    - [ ] 実機の空きRAM/Flashの実測 (Lua本体はflash100KB超・state 20〜30KBのオーダー)
  - [ ] Luaソースの動作 … platformio.ini と pc/CMakeLists.txt の両方へ追加が要る
  - [ ] LuaとC++をつなぐAPIの設計
  - [ ] APIの実装、検証
- [x] PC動作対応（Luaアプリ開発を便利に）… `pc/` (CMake + SDL2)
  - [x] LovyanGFX対応 … `lgfx::Panel_sdl`。`src/`は実機と同一のまま
  - [x] タッチ操作対応 … SDLのマウスをタッチとして読む
  - [x] ネイティブ関数の代替関数 … `pc/compat/` (Arduino/SPI/WiFi/SdFat)
  - [x] ブラウザ動作対応(WebAssembly) … `emcmake cmake -S pc -B pc/build-web`。
        `emscripten_set_main_loop`で1フレームずつ回す。SDカードは`index.data`へ同梱、
        設定はURLのクエリ(`?wifi=disconnected`)
  - [x] Webビルドの自動公開 … `main`へのpushでGitHub Pagesへ
        (https://kimu1109.github.io/pico-os/)。`.github/workflows/web-pages.yml`
  - [ ] その他あれば
- [ ] 標準アプリ開発 … 部品は揃っているがアプリ本体は未着手。
      現状ランチャに載っているのは MarkdownScene と InputTestScene(部品の動作確認用)だけで、
      **Lua APIの仕様を逆算するための実例が足りていない**
  - [ ] 設定アプリ … 部品: Config_Functions(読み書き) / Checkbox / DropdownMenu
  - [ ] 時計 … 部品: TimeFunctions(NTP同期済み)
  - [ ] 辞書 … 部品: IME_Functions(SKK辞書)
  - [ ] 電卓 … 部品: KeyboardNum / NumberInput が既にある(最小コストで1本書ける)
  - [ ] ファイルエクスプローラー … FileExplorerウィジェットは実装済み。アプリ(Scene+App_List登録)が無い
  - [ ] Markdownブラウザ … 「mdのウェブブラウザ」にする方針。通信仕様の草案は PROTOCOL.md
    - [x] リンク配線 + 履歴 + ナビゲーションヘッダー
          … MarkdownSceneが履歴(8件、パス+スクロール位置)を自前で持ち、
          PICO_IO::resolve()で相対リンクを解決して同じシーンのまま開き直す。
          ヘッダに戻る/進む/終了、フッタに現在のパス(エラー時はメッセージ)
    - [x] キャッシュ層 (storage/Doc_Cache) … /cache/<ホスト>/<パス> へミラーし、
          目録は /cache/index.tsv。Writerが一時ファイル(.part)→renameで差し替えるので
          通信が途中で切れた半端なファイルが残らない。1件64KiBで頭打ち。
          追い出しは作らない(全消去のClear()だけ)。**まだ呼び出し元は無く、繋ぐのは次の段**
    - [x] HttpGet(平文HTTP) + pc/compat の WiFiClient + 参照実装サーバ
          … util/Url(URL分解・相対解決) / net/Http_Response(ソケット非依存の増分パーサ、
          chunkedは検出してエラー) / task/Http_Get(Task派生、リダイレクト3回・10秒打ち切り・
          条件付きGET・3xx/4xxの本文はシンクへ流さない)。
          run_net.sh が参照実装サーバ相手に実通信で検証する。**まだ呼び出し元は無い**
    - [x] 取得→キャッシュ→表示の配線 (net/Doc_Fetch)
          … 条件付きGET→304ならキャッシュ据え置き/200なら差し替え、取得失敗時は
          古いキャッシュで代用(オフライン表示)。MarkdownSceneの履歴はSDパスとURLの
          どちらも載る。network.cfg の browser-home でホームを指定できる。
          **ここまででサーバ上の文書を読めるようになった**
    - [x] 画像の解決と先読み … 文書基準でパスを解決(MarkdownView::resolveRef、
          レイアウトと表示の2箇所)。リモート文書では表示前に1フレーム1枚ずつ取得し、
          1枚失敗したら残りは諦める。1ページ8枚まで、キャッシュ済みは取りに行かない
    - [x] discovery(/.well-known/pico-os) … ただの文書として取るので条件付きGETも
          オフライン時の据え置きもそのまま効く。知らないキーは無視(前方互換)、
          searchの行が無ければ検索非対応、404は「素の静的サーバ」として確定扱い。
          homeが申告されていればヘッダの「ホーム」ボタンの行き先になる
    - [x] 遷移に失敗したときの巻き戻し … 履歴の現在地は**相対リンクの基準**でもあるので、
          開けなかった場所を現在地のまま残すと、画面には前の文書が出ているのに
          次に踏んだリンクだけがその場所を基準に解決される。失敗したら
          abortNavigation()で表示中の位置まで戻す(サーバ情報も引き直す)
    - [x] リロードボタン(キャッシュを無視して取り直す) … ヘッダの「更新」。
          検証子を送らないので304ではなく200が返り、キャッシュごと差し替わる。
          挿絵も引き直す(文書だけ新しくて絵が古いままにならないように)。
          履歴は積まずスクロール位置も保つ
    - [x] 検索(TSV) + ScrollListの結果画面 … ヘッダの「検索」→ InputDialogで語を聞き、
          SearchDialog(ScrollList)に結果を出す。2回タップで開く。
          **検索だけはDoc_Cacheを通さない**(キャッシュのキーがクエリを見ないため、
          通すと検索語違いの結果が同じファイルへ重なる)。10件ずつで「次へ」あり。
          discoveryにsearchの行が無いサーバでは検索ボタンを灰色にする
    - [x] マニフェスト(/v1/manifest)でキャッシュを一括再検証する … discoveryの直後に1回引き、
          手元の検証子とversionが一致する文書は**サーバへ何も聞かずに**開く(304の往復も無し)。
          一致しない/載っていない/非対応サーバなら今までどおり条件付きGETへ落ちる。
          「更新」を押すとマニフェストも引き直す
- [ ] セカンダリアプリ開発
  - [ ] チャットツール
  - [ ] オセロ風
  - [ ] テトリス風
  - [ ] PICO SHOOTING
  - [ ] ブロック崩し風
  - [ ] リマインダー
  - [ ] カレンダー
- [ ] GameBoyエミュ
  - [ ] 最適なGBエミュを探せ
  - [ ] GBエミュを適合させよ
  - [ ] 快適動作を目指そう
- [ ] 外部コントローラー
  - [ ] 配線/方式を考えよう
  - [ ] 入力を受け取ろう
  - [ ] 実際にアプリに組み込めるようにしよう
- [ ] Chiptuneを再生
  - [ ] とりあえず再生
  - [ ] GB対応
  - [ ] 標準ファイル形式を探す/考える
  - [ ] アプリ対応
