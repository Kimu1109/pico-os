---
title: "JSON"
weight: 75
description: "json_decode / json_encode / json_null / json_pick / sd_json_pick"
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

## pico.json_pick

<div class="sig">pico.json_pick(text: string, paths: {string} [, opts: table]) <span class="ret">-> values: table, truncated: table | nil</span><br>
<span class="ret">-> nil, error: string</span></div>

JSONから**指定したパスの値だけ**を取り出します。`json_decode` は文書全体をLuaの表にするので、数KB〜数十KBの応答から数個の値が欲しいだけのときにLuaのメモリを大きく使います。`json_pick` は流しながら読み(C++側で確保しない)、結果の値だけをLuaに作ります。

- パスはキーを `.` でつなぎ、配列は**1始まり**の番号で書きます(`"item.artists.1.name"`)。一番外が配列なら `"2"`、`"2.x"`。
- パスの最後に `#` を付けると、その配列/オブジェクトの**要素の数**になります(`"item.artists#"`、一番外は `"#"`)。
- 結果は `{[パス] = 値}` の表です。値は文字列・数(整数は整数)・真偽値。見つからないパスと `null` は入りません(`opts.keep_null = true` なら `null` は `pico.json_null`)。オブジェクトや配列そのものは取れません(中の値か `#` を指定します)。
- 文字列は255バイトまでで、長いものは文字の途中で切らずに切り詰めます。切り詰めた値があれば2番目の戻り値に `{[パス] = true}` が入ります(無ければ `nil`)。
- パスは1〜32個、1つ510バイトまで。キーは47バイトを超える分が切られて比べられます(長いキーの中は取れません)。入れ子は16段まで。
- JSONが不正なら `nil, 理由`。頼んだ値が全部見つかっていれば、その後ろが途中で切れていても成功にします。

```lua
local r = pico.json_pick(body, {"is_playing", "progress_ms", "item.name", "item.artists.1.name",
                                "item.duration_ms", "item.album.images#"})
if r then
    title = r["item.name"] or ""
    artist = r["item.artists.1.name"] or ""
    playing = r.is_playing == true
end
```

## pico.sd_json_pick

<div class="sig">pico.sd_json_pick(path: string, paths: {string} [, opts: table]) <span class="ret">-> values: table, truncated: table | nil</span><br>
<span class="ret">-> nil, error: string</span></div>

`json_pick` のSDのファイル版です。`pico.http_request` の `save_to` で保存した、16KiBを超える応答を読むのに使います。ファイルは1MiBまで。頼んだ値が全部見つかった時点で残りは読みません。
`sd_read` と同じく、`sd_outside_app_dir` 権限が無ければアプリのフォルダの外は読めません(`nil, 理由`)。
