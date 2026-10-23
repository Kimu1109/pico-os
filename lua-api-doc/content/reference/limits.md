---
title: "上限"
weight: 50
description: "メモリ・実行時間・数の範囲・各種サイズの上限"
---

## メモリ・実行時間

| 項目 | 値 | 説明 |
|---|---|---|
| Luaが使えるメモリ | 200KB(204,800バイト) | 1アプリあたり。コード・テーブル・文字列など全部を含む。ウィジェットと画像は別枠 |
| 命令数(1回の呼び出しあたり) | 200万命令 | 超えるとその呼び出しはエラーで打ち切られる。[実行時間とメモリの上限](../../guide/app/execution-limits/) |
| `main.lua` の大きさ | 16KiB | 超えた分は切り捨てて読む(たいてい構文エラーになる) |
| `require` で読む1ファイル | 32KiB | 先に読み込めるのは1つのスクリプトにつき16モジュールまで |
| 同時に存在できるウィジェット | 1,023個 | |
| バックグラウンドのスクリプト | メモリ48KB・1回30秒 | [バックグラウンド](../../api/system/background/) |

## 数の範囲(32bitのLua)

pico-os の Lua は **32bitの整数と32bitの小数(単精度)** で動きます。

- 整数は `math.mininteger`(-2147483648)〜`math.maxinteger`(2147483647)。超えると黙って回ります(`2147483647 + 1 == -2147483648`)。`0xffffffff` は `-1` です
- 小数の有効桁は約7桁。`16777217.0 == 16777216.0` になります。エポック秒(約17億)や大きなミリ秒を小数で割ると端数が落ちるので、時刻の差は整数のまま引いてから割ってください
- `tostring` は小数を7桁で書きます(`0.1` → `"0.1"`)。JSONの書き出しも同じ
- `pico.time()` / `os.time()` のエポック秒は2038年1月まで整数に収まります

## サイズの上限

| 項目 | 値 |
|---|---|
| `pico.sd_read()` / `sd_read_part()` で1回に読める大きさ | 16KiB(超えるファイルは `sd_read` が `nil`。切り詰めない) |
| `pico.http_request()` で送る本文 | 16KiB |
| `pico.http_request()` で受け取る本文 | 16KiB(メモリで受けるとき。`save_to` でSDへ保存するなら8MiB) |
| `pico.http_request()` の順番待ち | 走っている1本 + 待ち4本 |
| `pico.http_request()` の追加ヘッダ | 合計1000バイト、1行(名前: 値)は512バイト未満 |
| `pico.after` / `pico.every` のタイマー | 16個 |
| `push_scene` の `args` / `pop` の `result` | JSONで1KiB未満 |
| `on_suspend` が返す状態 | JSONで2KiB未満 |
| `pico.store_save()` | JSONで16KiB(暗号化するときは12KiB) |
| `pico.json_decode` の入力 / `json_encode` の出力 | 64KiB。入れ子は16段 |
| `pico.json_pick` / `xml_pick` の文字列の値 | 255バイト(`sd_json_pick` / `sd_xml_pick` のファイルは1MiB) |
| 同時に持てる画像(`image_load` / `image_create`) | 8枚・合計96KiB |
| `TabBar` のタブ | 8個 |
| `pico.tab_link` の連動 | 32個 |
| `pico.set_name` の名前 | 128個・各23文字まで |
| `pico.set(id, "text", ...)` に渡せる文字列 | 255バイト(長い文章は `pico.text_set`) |
| `TextView` の文章(`pico.text_set`) | 16KiB(1024行まで表示) |
| `MarkdownView` の文書 | 8KiB |
| `pico.show_choice` の選択肢 | 64個 |
| `pico.show_message` の本文 | 512バイト |
| `pico.draw_text_wrapped` | 2048バイト・64行 |
| `pico.draw_polygon` / `fill_polygon` | 32点 |
| `pico.notify` の予約 | 1アプリ4件(OS全体で16件) |
| `pico.encrypt` の平文 | 12KiB |
| `pico.random_bytes` | 1〜1024バイト |
| `pico.base64_encode` / `decode` の入力 | 12KiB / 16KiB |
| `pico.clipboard_set` | 1KiB |

## HTTPS

- `https://` で信頼するのは、本体に入っているルート証明書(Google / Let's Encrypt / DigiCert / Sectigo)と、SDの `/sys/tls/ca.pem` です。
- 本体の時計が合う(Wi-Fiにつながって時刻を合わせる)前は繋がりません。
