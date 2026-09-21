---
title: "シーン制御"
weight: 50
description: "pop / push_scene / change_scene / launch_app / content_rect"
---

## pico.pop

<div class="sig">pico.pop() <span class="ret">-> (なし)</span></div>

自分を起動した画面(ランチャ、または `push_scene` の呼び出し元)へ戻ります。要求はフレーム境界まで保留されます。

## pico.push_scene

<div class="sig">pico.push_scene(path: string) <span class="ret">-> (なし)</span></div>

`path` の Lua スクリプトを新しい画面として開きます。今の画面はスタックへ退避され、新しい画面で `pico.pop()` すれば戻れます。今の `LuaEngine` が持つ権限(`LuaPermissions`)がそのまま引き継がれます。

## pico.change_scene

<div class="sig">pico.change_scene(path: string) <span class="ret">-> (なし)</span></div>

`path` の Lua スクリプトへ画面を置き換えます。スタックを消費しないため、今の画面へは戻れません(`pico.pop()` すると、今の画面を開いた側まで直接戻ります)。権限は `push_scene` と同じく引き継がれます。

## pico.launch_app

<div class="sig">pico.launch_app(name: string) <span class="ret">-> ok: boolean</span></div>

ランチャの登録簿にある任意のアプリ(C++製アプリを含む)を名前で探し、見つかれば起動します。見つからなければ `false`。見つかった場合の戻り値は `true` ですが、実際の画面遷移自体の成否(スタック上限等)までは判定していません。権限は引き継がれず、遷移先アプリ自身の権限に従います。

## pico.content_rect

<div class="sig">pico.content_rect() <span class="ret">-> x: integer, y: integer, w: integer, h: integer</span></div>

ステータスバーを除いた、アプリが自由に使える描画領域を返します。C++製アプリと同じ基準です。
