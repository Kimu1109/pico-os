---
title: "JSON"
weight: 75
description: "json_decode / json_encode / json_null"
---

HTTPの応答や設定ファイルでよく使うJSONを、LuaのテーブルとC++で相互変換します。純Luaで書くより速く、スクリプトの上限(16KiB)も食いません。

## pico.json_decode

<div class="sig">pico.json_decode(text: string [, keep_null: boolean]) <span class="ret">-> value | nil, error: string | nil</span></div>

JSON文字列をLuaの値にします。失敗すると `nil` と理由(誤りの位置つき)を返します。

```lua
local t, err = pico.json_decode('{"name":"たろう","scores":[10,20.5],"ok":true}')
if not t then pico.log("JSONが不正: " .. err) return end
print(t.name, t.scores[2], t.ok)   -- たろう 20.5 true
```

- 整数は整数(`math.type` が `"integer"`)、小数や `1e3` のような指数表記は小数になります。Luaの整数(32bit、約±21億)に収まらない整数は小数になります(下の「数の範囲」)。
- 文字列のエスケープ(`\n` `\"` `\u3042` とサロゲートペア)はUTF-8に戻されます。対になっていないサロゲートは `U+FFFD` です。
- `null` は既定では `nil` になります(オブジェクトのキーは消え、配列は穴になります)。`keep_null` を `true` にすると `pico.json_null` という目印に置き換わるので、`null` と「キーが無い」を区別できます。
- 入れ子は16段、入力は64KiBまでです。超えると失敗します。
- 前後の空白は許されますが、末尾の余分な文字やカンマ、単引用符、`01` のような先頭の0は不正です。

## pico.json_encode

<div class="sig">pico.json_encode(value) <span class="ret">-> text: string | nil, error: string | nil</span></div>

Luaの値をJSON文字列にします。失敗すると `nil` と理由を返します。

```lua
local body = pico.json_encode({ name = "たろう", scores = {10, 20}, ok = true })
pico.http_request("POST", url, body, "application/json", on_done)
```

- キーが `1..n` の整数のテーブルは配列になります(穴は `null` で埋めます: `{1,nil,3}` → `[1,null,3]`)。キーが文字列のテーブルはオブジェクトになります。**空のテーブルは `[]`** になります。
- 書けないもの: 関数・スレッド・userdata、`NaN` と無限大、文字列以外のキーを持つオブジェクト、入れ子が16段を超えるテーブル(循環参照も同じ理由で失敗します)、出力が64KiBを超えるもの。
- オブジェクトのキーの順序は決まっていません(出力を文字列として比べず、`json_decode` で戻して比べてください)。
- `pico.json_null` と `nil`(トップレベル)は `null` になります。

## pico.json_null

<div class="sig">pico.json_null <span class="ret">-- 値(lightuserdata)</span></div>

`json_decode(text, true)` が `null` の代わりに返す目印で、`json_encode` に渡すと `null` になります。
