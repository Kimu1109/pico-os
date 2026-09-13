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
    - [ ] AppEntryの動的化 … create()が引数なし関数ポインタ、nameが静的リテラル必須で、
          「同じLuaScene型+別スクリプトパス」を登録できない。SD走査での動的登録の口も無い
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
  - [ ] その他あれば
- [ ] 標準アプリ開発 … 部品は揃っているがアプリ本体は未着手。
      現状ランチャに載っているのは MarkdownScene と InputTestScene(部品の動作確認用)だけで、
      **Lua APIの仕様を逆算するための実例が足りていない**
  - [ ] 設定アプリ … 部品: Config_Functions(読み書き) / Checkbox / DropdownMenu
  - [ ] 時計 … 部品: TimeFunctions(NTP同期済み)
  - [ ] 辞書 … 部品: IME_Functions(SKK辞書)
  - [ ] 電卓 … 部品: KeyboardNum / NumberInput が既にある(最小コストで1本書ける)
  - [ ] ファイルエクスプローラー … FileExplorerウィジェットは実装済み。アプリ(Scene+App_List登録)が無い
  - [ ] Markdownブラウザ … MarkdownScene は登録済みだが開く文書が tmp/doc.md 固定。
        FileSelectDialog との接続が未了
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
