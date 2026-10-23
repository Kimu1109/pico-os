---
title: "アプリのフォルダと app.cfg"
weight: 10
description: "アプリのフォルダに置くファイルと、app.cfg に書ける設定の一覧"
---

## フォルダの中身

```
/lua/apps/わたしのゲーム/
  main.lua        必須。アプリを開くと実行される
  app.cfg         任意。名前・アイコン・権限などの設定
  icon.pimg       任意。ランチャのアイコン(app.cfg の icon で指定)
  util.lua        任意。require("util") で読むモジュール
  ui/menu.lua     任意。require("ui.menu") で読むモジュール
  background.lua  任意。閉じている間に動かすスクリプト(app.cfg の background で指定)
  store.json      pico.store_save() が書くセーブデータ
  settings.cfg    pico.settings_set() が書く設定
```

- `main.lua` は16KiBまでです。大きくなったら機能ごとにファイルを分け、[`require`](../../api/system/modules/) で読みます(1ファイル32KiBまで)。日本語のコメントは1文字3バイトなので、コメントの多いスクリプトは早めに分けてください。
- 同じフォルダに `lib.lua` があると `main.lua` より先に実行されます(古い書き方です。新しいアプリは `require` を使ってください)。
- 画像やデータのパスは、`pico.path_join(pico.app_dir(), "img", "title.pimg")` のようにアプリのフォルダから組み立てます(フォルダ名を書き込まずに済みます)。
- アプリが書いたファイル(`store.json` など)は、アプリストアでアプリを更新しても残ります。

## app.cfg

1行に1つ `key=value` で書きます。`#` で始まる行はコメントです。知らないキーは無視されます。

```
name=わたしのゲーム
icon=icon.pimg
id=my-game
version=1.0.0
permission_network=true
```

| キー | 既定 | 意味 |
|---|---|---|
| `name` | フォルダ名 | ランチャに出す名前 |
| `description` | なし | アプリの説明 |
| `icon` | 標準のアイコン | ランチャのアイコン。アプリのフォルダからの相対パスで、48×48の `.pimg` |
| `id` | なし | アプリストアで配るときの名前(英小文字・数字・`-`・`_`、23文字まで)。[アプリストアで配る](../../guide/app/publishing/) |
| `version` | なし | 版(`1.2.0` のような数字と `.`)。アプリストアで更新を見分けるのに使う |
| `permission_network` | `false` | HTTP通信を許す |
| `permission_sd_outside_app_dir` | `false` | アプリのフォルダの外のSDを読み書きできるようにする |
| `permission_notify` | `false` | 通知(`pico.notify`)を許す |
| `permission_microphone` | `false` | マイクを許す |
| `permission_background` | `false` | 閉じている間のスクリプト(`background`)を許す |
| `background` | なし | 閉じている間に動かすスクリプト。[バックグラウンド](../../api/system/background/) |
| `background_interval` | `900` | `background` を動かす間隔(秒、10以上) |
| `strip_debug` | `false` | `require` したモジュールの行番号を落としてメモリを減らす。[モジュール](../../api/system/modules/#デバッグ情報を落としてメモリを減らすappcfg-の-strip_debug) |

権限の意味と、権限が無いときの動きは [権限](../../guide/app/permissions/) にまとめてあります。

`app.cfg` はアプリ自身からは書き換えられません(スクリプトが自分で権限を足せないように)。アプリの設定は `settings.cfg` など別のファイルへ書いてください。

## 画像(.pimg)を作る

アイコンや絵は、OS独自の `.pimg`(16色・RLE圧縮)が一番軽く読めます。PNGなどからは、リポジトリの `script/generate_pimg.py` で変換します。BMPとJPEGもそのまま読めます([画像](../../guide/graphics/images/))。
