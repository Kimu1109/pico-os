---
title: "シーン制御"
weight: 50
description: "pop / push_scene / change_scene / launch_app / content_rect / args / store_load / store_save"
---

## pico.pop

<div class="sig">pico.pop([result]) <span class="ret">-> (なし)</span></div>

自分を起動した画面(ランチャ、または `push_scene` の呼び出し元)へ戻ります。要求はフレーム境界まで保留されます。

`result`(任意)を渡すと、`push_scene` の呼び出し元の画面の **`on_result(result)`** に渡されます([画面の間で値を受け渡す](#画面の間で値を受け渡す) 参照)。`result` はJSONにできる値(テーブル・文字列・数値・真偽値)で、JSONにして1KiB未満である必要があります。大きすぎる・JSONにできない値はエラーです。ランチャから起動した画面など、戻り先がLuaの画面でなければ結果は捨てられます。

## pico.push_scene

<div class="sig">pico.push_scene(path: string [, args]) <span class="ret">-> (なし)</span></div>

`path` の Lua スクリプトを新しい画面として開きます。今の画面はスタックへ退避され、新しい画面で `pico.pop()` すれば戻れます。今の `LuaEngine` が持つ権限(`LuaPermissions`)がそのまま引き継がれます。

`args`(任意)は新しい画面の **`pico.args()`** で受け取れます。`pico.pop` の `result` と同じく、JSONにできる値で1KiB未満です。

## pico.change_scene

<div class="sig">pico.change_scene(path: string [, args]) <span class="ret">-> (なし)</span></div>

`path` の Lua スクリプトへ画面を置き換えます。スタックを消費しないため、今の画面へは戻れません(`pico.pop()` すると、今の画面を開いた側まで直接戻ります)。権限は `push_scene` と同じく引き継がれます。`args` も同じ。置き換えた先の `pico.pop(result)` は、元の画面を開いた側の `on_result` に届きます。

## pico.args

<div class="sig">pico.args() <span class="ret">-> args | nil</span></div>

`push_scene` / `change_scene` で渡された `args` を返します。何も渡されなければ `nil`。`null` は `pico.json_null` になります。

## pico.launch_app

<div class="sig">pico.launch_app(name: string) <span class="ret">-> ok: boolean</span></div>

ランチャの登録簿にある任意のアプリ(C++製アプリを含む)を名前で探し、見つかれば起動します。見つからなければ `false`。見つかった場合の戻り値は `true` ですが、実際の画面遷移自体の成否(スタック上限等)までは判定していません。権限は引き継がれず、遷移先アプリ自身の権限に従います。

## pico.content_rect

<div class="sig">pico.content_rect() <span class="ret">-> x: integer, y: integer, w: integer, h: integer</span></div>

ステータスバーを除いた、アプリが自由に使える描画領域を返します。C++製アプリと同じ基準です。

## 画面の間で値を受け渡す

`push_scene` で別の画面へ移って `pop` で戻ると、**元の画面のスクリプトは最初から実行し直されます**(Luaの状態はC++側から退避できないため)。値を受け渡すための仕組みが3つあります。

| やりたいこと | 使うもの |
|---|---|
| 子の画面に値を渡す | `push_scene(path, args)` → 子で `pico.args()` |
| 子の画面から結果を受け取る | 子で `pico.pop(result)` → 親の `on_result(result)` |
| 離れる前の状態を戻ったあとに復元する | `on_suspend()` が返したテーブル → `on_resume(state)` |

```lua
-- main.lua(親)
local selected = nil
function on_suspend()               -- 画面を離れる直前に呼ばれる
    return { selected = selected }  -- 返したテーブルが持ち越される(JSONで2KiB未満)
end
function on_resume(state)           -- 戻ってきて setup() の後に呼ばれる
    selected = state.selected
end
function on_result(r)               -- 子が pico.pop(result) したとき、on_resume の後に呼ばれる
    selected = r.color
    pico.set(label, "text", "選んだ色: " .. r.color)
end
pico.on(pick_btn, "press_start", function()
    pico.push_scene("/lua/apps/demo/picker.lua", { title = "色を選ぶ" })
end)

-- picker.lua(子)
local args = pico.args()
pico.on(red_btn, "press_start", function()
    pico.pop({ color = "red" })
end)
```

- 呼ばれる順序は `setup()` → `on_resume(state)` → `on_result(result)` です。どれも定義は任意で、無ければ何も起きません。
- `on_suspend` は**画面を離れるたび**(`push_scene` で別の画面へ移るとき、`pop` で戻るとき)に呼ばれます。返す値が無い(`nil`)ときは何も保存されません。ここでエラーになってもダイアログは出ず、ログに残るだけです。
- `on_result` は、`pico.pop(result)` から約3秒以内に親が実際に戻ったときだけ呼ばれます。子の画面から `pico.pop()` した(結果を渡さなかった)ときは呼ばれません。
- 持ち越した状態が残るのは、その画面がスタックに積まれている間(メモリ上)だけです。アプリを閉じた後も残したい値は `store_save` を使ってください。

## pico.store_load / pico.store_save

<div class="sig">pico.store_load() <span class="ret">-> value | nil</span></div>
<div class="sig">pico.store_save(value) <span class="ret">-> ok: boolean</span></div>

アプリのフォルダの `store.json` に値を1つ保存・読み込みます(最高得点・設定・途中経過など)。`sd_read`/`sd_write` と `json_encode`/`json_decode` を組み合わせた便利関数です。

```lua
local save = pico.store_load() or { best = 0 }
if score > save.best then
    save.best = score
    pico.store_save(save)
end
```

- 保存できるのは JSON にできる値で、**16KiBまで**です。JSONにできない値(関数など)を渡すとエラー、大きすぎる・書き込めないときは `false` を返します。
- 一時ファイルに書いてから差し替えるので、書き込み中に電源が切れても前の内容は壊れません。
- ファイルが無い・壊れている・SDが使えないときは `nil` を返します。
- アプリのフォルダの外には書きません(`sd_outside_app_dir` の権限に関わらず固定です)。
