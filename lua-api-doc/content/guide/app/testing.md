---
title: "PCでの開発(ホットリロードとテスト)"
weight: 35
description: "保存するたびにPC版で開き直すホットリロードと、picoos lua-test で動かす自動テスト"
---

リポジトリの `cli-tool/picoos`(開発用のコマンドをまとめたもの)を使うと、PCだけでアプリを作って確かめられます。

## ホットリロード

```sh
picoos pc build                                  # はじめに一度(リポジトリの pc/README.md)
picoos pc dev pc/sdcard/lua/apps/わたしのゲーム    # アプリのフォルダか、ランチャの名前
```

PC版がそのアプリを開いた状態で起動し、アプリのフォルダの **`.lua` と `app.cfg` を保存するたびに起動し直します**(保存から1秒ほどで新しいコードの画面になります)。下のフォルダ(`ui/menu.lua` など)も見ます。

- 起動し直すのはPC版ごとです。タイマーや画像など、前のコードが持っていたものは残りません。
- SDへ書いたもの(`store.json`・`settings.cfg` など)は残るので、保存データの続きから試せます。アプリが書くファイルは見ていないので、保存しても起動し直しません。
- 構文エラーがあればいつものエラーダイアログが出ます。直して保存すればまた起動し直します。
- `--scene 画面` で最初に開く画面を頼めます(`pico.launch_reason()` が `"scene"` を返します)。`--` の後ろはPC版へそのまま渡ります。

```sh
picoos pc dev テトリス --scene play
```

直接起動するなら `picoos_pc --app 名前 --hot-reload` です。ブラウザ版(Playground)にはありません。

## 自動テスト(picoos lua-test)

アプリのフォルダの `tests/` に `*_test.lua` を置くと、`picoos lua-test` で動かせます。アプリは `pico.*` の偽物の上で動くので、実機もPC版のビルドも要りません(Cコンパイラだけ使います)。

```
/lua/apps/わたしのゲーム/
  main.lua
  tests/
    menu_test.lua
```

```lua
-- tests/menu_test.lua
test("スタートを押すと得点が出る", function()
    local app = load_app()          -- main.lua を読み、setup() まで呼ぶ
    app:tap("スタート")              -- text が「スタート」のウィジェットを押して離す
    app:frame(100)                  -- 100ms進めて loop(100) を呼ぶ
    eq(app:widget("score").text, "0")
end)
```

```sh
picoos lua-test pc/sdcard/lua/apps/わたしのゲーム   # フォルダの下の *_test.lua を全部
picoos lua-test path/to/menu_test.lua              # 1ファイル
picoos lua-test                                    # pc/sdcard/lua/apps/ の全アプリ
picoos lua-test -k スタート -v                      # 名前で絞る / print とトレースバックも出す
```

```
== pc/sdcard/lua/apps/わたしのゲーム/tests/menu_test.lua
[ OK ] スタートを押すと得点が出る
[FAIL] 2回押すと2点
       tests/menu_test.lua:14: eq: 期待 "2" / 実際 "1"
-- 1件成功 / 1件失敗 (1ファイル)
```

失敗が1件でもあれば終了コードは1です。`tests/` と `*_test.lua` はアプリストアで配るときには入りません。

### テストの中で使える関数

| 関数 | 意味 |
|---|---|
| `test(名前, 関数)` | テストを1つ登録する。上から順に動く |
| `before_each(関数)` / `after_each(関数)` | 各テストの前後に呼ぶ(`after_each` は失敗しても呼ぶ) |
| `load_app(opts)` | アプリを読み込んで `app` を返す(下)。テストごとに呼ぶと毎回まっさらから |
| `ok(値, メッセージ)` | 真なら通る |
| `eq(実際, 期待, メッセージ)` / `ne(...)` | 同じなら(違えば)通る。表は中身まで比べ、違う所を出す |
| `near(実際, 期待, 許容差)` | 小数を比べる(既定の許容差は `1e-4`。実機と同じく小数は単精度) |
| `raises(関数, 文字列)` | エラーになれば通る。文字列を渡すとメッセージに含むかも見る |
| `fail(メッセージ)` | その場で失敗にする |

`load_app` の `opts`(全部省略できます):

| キー | 意味 |
|---|---|
| `args` | `pico.args()` が返す値 |
| `launch_reason` | `{ tag, data }`。`pico.launch_reason()` が返す値 |
| `setup = false` | `setup()` を呼ばない |
| `stubs` | 偽物の `pico.*` を足す・差し替える(`{ battery = function() return 50 end }`) |
| `dir` | アプリのフォルダ(既定はテストファイルの上の `main.lua` のあるフォルダ) |
| `main` | 最初に読むファイル(既定 `main.lua`。`push_scene` で開くページを試すとき) |

`app` でできること:

| 書き方 | 意味 |
|---|---|
| `app:widget(q)` | ウィジェットのプロパティの表(`pico.set` した値。`type` `x` `y` `w` `h` `text` `items` …) |
| `app:id(q)` / `app:find_all{ type = "Button" }` | ID / 条件に合うIDの一覧 |
| `app:tap(q, x, y)` | `press_start` → `press_end`(座標を省くとウィジェットの真ん中) |
| `app:fire(q, "changed", ...)` | 任意のイベントを鳴らす |
| `app:frame(ms)` / `app:run(合計ms, 1回のms)` | タイマーを進めて `loop(dt)` を呼ぶ |
| `app:key("enter")` | `pico.on_key` の関数を呼ぶ(文字か名前) |
| `app:back()` | 戻る(`pico.on_back` の関数、無ければ閉じる) |
| `app:last_dialog()` / `app:close_dialog(id, ok, 値)` | 出したダイアログ / 閉じて `closed` を鳴らす |
| `app:respond(番号, ok, status, body)` | `pico.http_request` の番号番目の要求へ応答する |
| `app:call("名前", ...)` / `app.env` | アプリのグローバル関数を呼ぶ / アプリのグローバル変数 |
| `app.mock` | 記録: `scene`(開いた画面)・`popped`(閉じた)・`draws`(描いた回数)・`dialogs`・`requests`・`toasts`・`logs`(`print`)・`sd`(書いたファイル) |

`q` はウィジェットのID、`pico.set_name` の名前、または `text` が一致する1つです(見つからない・2つ以上ならエラー)。

### 偽物でできないこと

- **描画はしません**(回数を数えるだけ)。見た目はホットリロードかPC版の画面で確かめてください。
- ウィジェットはプロパティの入れ物で、並べたり当たり判定をしたりしません。`tap` はそのウィジェットのイベントを直接鳴らします。
- SDは、アプリのフォルダの中は本物のファイルを読み、書き込み・削除はテストの中だけで消えます(本物のファイルは変えません)。`pico.sd_list` はテストの中で書いたファイルしか見えません。
- 偽物に無い `pico.*` を呼ぶと `pico_mock に無い関数です: pico.xxx` で止まります。`stubs` で足してください。
- アプリは実機の[サンドボックス](../debugging/#サンドボックス)と同じく `io` や `os.exit` が見えない環境で動きます。`require` はアプリのフォルダと `pico.*` の同梱モジュール(本物)を読みます。数の扱い(整数32bit・単精度の小数)も実機と同じです。
