---
title: "その他"
weight: 90
description: "log / show_error"
---

## pico.log

<div class="sig">pico.log(message: string) <span class="ret">-> (なし)</span></div>

システムログへメッセージを出力します(アプリログ扱い)。デバッグ用途に使います。

## pico.show_error

<div class="sig">pico.show_error(message: string) <span class="ret">-> (なし)</span></div>

エラーログを出したうえで、メッセージダイアログをその場で表示します。「ユーザーへ見せるべき失敗」を通知する共通の窓口です。Luaスクリプト自体の構文エラー・実行時エラー(捕捉されなかったもの)も、内部的にはこれと同じ経路で表示されます。
