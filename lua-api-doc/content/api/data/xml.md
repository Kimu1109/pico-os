---
aliases: ["/api/xml/"]
title: "XML"
weight: 30
description: "xml_pick / sd_xml_pick"
---

RSS/AtomのようなXMLから、必要な値だけを取り出します。文書全体をLuaの表にはせず、先頭から読み流して頼んだ値だけを作ります。Google NewsのRSS(`https://news.google.com/rss/search?q=...&hl=ja&gl=JP&ceid=JP:ja`)の題名やリンクを拾う使い方を想定しています。

## pico.xml_pick

<div class="sig">pico.xml_pick(text: string, paths: {string} [, opts: table]) <span class="ret">-> values: table, truncated: table | nil</span><br>
<span class="ret">-> nil, error: string</span></div>

- パスは要素名を `.` でつなぎます。**一番外(ルート要素)から書きます**(`"rss.channel.item.title"`)。
- 同じ名前の兄弟は、番号を省くと**1番目**です。名前の直後に1始まりの番号を置くと選べます(`"rss.channel.item.2.title"`)。
- 属性は要素のパスの後ろに `@属性名` を付けます(`"rss.channel.item.source@url"`、ルートの属性は `"rss@version"`)。
- 末尾に `#` を付けると、その名前の**子の数**になります(`"rss.channel.item#"`)。数えるために最後まで読みます。
- 結果は `{[パス] = 値}` の表で、値は文字列です(数にはしません)。見つからないパスは入りません。
- 要素の文字は前後の空白を落とし、`&lt;` `&amp;` `&#12354;` などの実体参照とCDATAを戻した文字になります(`&nbsp;` のような未定義の実体は書かれたまま)。子要素をはさんだ文字は最初のひとまとまりだけです。
- 文字・属性値は255バイトまでで、超えた分は文字の途中で切らずに切り詰めます。切り詰めた値は2番目の戻り値に `{[パス] = true}` で入ります。
- 名前空間は解決せず、書かれたまま(`media:content`)で指定します。
- パスは1〜32個、1つ510バイトまで。入れ子は16段まで。同じ親の下で数えられる名前の種類は16まで(超えた名前は常に1番目として扱われます)。
- XMLが不正なら `nil, 理由`。頼んだ値(`#` 以外)が全部見つかっていれば、その後ろが途中で切れていても成功にします。

```lua
local r = pico.xml_pick(body, {"rss.channel.item.1.title", "rss.channel.item.1.link",
                               "rss.channel.item.1.source@url", "rss.channel.item#"})
if r then
    print(r["rss.channel.item.1.title"], r["rss.channel.item#"])
end
```

## pico.sd_xml_pick

<div class="sig">pico.sd_xml_pick(path: string, paths: {string} [, opts: table]) <span class="ret">-> values: table, truncated: table | nil</span><br>
<span class="ret">-> nil, error: string</span></div>

`xml_pick` のSDのファイル版です。`pico.http_request` の `save_to` で保存した、16KiBを超える応答を読むのに使います。ファイルは1MiBまで。頼んだ値が全部見つかった時点で残りは読みません(`#` を頼むと最後まで読みます)。
`sd_read` と同じく、`sd_outside_app_dir` 権限が無ければアプリのフォルダの外は読めません(`nil, 理由`)。
