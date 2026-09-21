---
title: "Hello Lua(基本APIの一通り)"
weight: 10
description: "pc/sdcard/lua/hello.lua — ボタン・画像・時刻・イベントなど基本APIをひと通り使う例"
---

`pc/sdcard/lua/hello.lua` は `App_List.cpp` に手動登録されている(SD走査ではない)デモアプリです。`sd_outside_app_dir=true` の権限を持つため、`/lua/apps/` の外(`/img/hello.pimg`)を読めます。

## 配置とレイアウトの起点

```lua
local x, y, w, h = pico.content_rect()
local margin = 10
```

すべてのウィジェットはこの `x, y, w, h` を起点に配置します。ステータスバーの下に潜り込むことがありません。

## カウンタボタン

```lua
local count = 0
local count_label = pico.create("Label")
pico.set(count_label, "text", "count: 0")

local inc_button = pico.create("Button")
pico.set(inc_button, "text", "+1")
pico.on(inc_button, "press_start", function()
    count = count + 1
    pico.set(count_label, "text", "count: " .. count)
end)
```

`press_start` イベントでLua変数(`count`)を更新し、`pico.set()` でラベルへ反映します。関連: [ウィジェットの生成・操作・破棄](../../guide/widgets/)。

## 画像の読み込みと表示

```lua
local hello_img = pico.image_load("/img/hello.pimg")
local img_w, img_h = 48, 24 -- 読み込みに失敗した場合のフォールバック値
if hello_img then
    img_w, img_h = pico.image_size(hello_img)
end

local image_canvas = pico.create("Canvas")
pico.set(image_canvas, "w", img_w)
pico.set(image_canvas, "h", img_h)
pico.on(image_canvas, "render", function()
    if hello_img then
        pico.draw_image(hello_img, x + margin, y + margin + 190)
    end
end)
```

`pico.image_load()` の失敗に備えて既定サイズを用意しておくパターンです。描画は必ず `Canvas` の `render` コールバックの中で行います。関連: [画像](../../guide/images/)。

戻るボタンでは、使い終わった画像を明示的に解放しています。

```lua
pico.on(back_button, "press_start", function()
    if hello_img then
        pico.image_free(hello_img)
        hello_img = nil
    end
    pico.pop()
end)
```

## 直接描画で顔を描く

```lua
local canvas = pico.create("Canvas")
pico.on(canvas, "render", function()
    local BLACK = 0
    pico.draw_circle(cx, cy, 28, BLACK)
    pico.fill_circle(cx - 10, cy - 8, 3, BLACK)
    pico.fill_circle(cx + 10, cy - 8, 3, BLACK)
    pico.draw_line(cx - 12, cy + 10, cx + 12, cy + 10, BLACK)
    pico.draw_text(cx - 30, cy + 34, "canvas", BLACK)
end)
```

生成直後のウィジェットは初期状態でdirtyなので、追加の操作なしで一度 `render` が呼ばれます。関連: [Canvasと直接描画](../../guide/drawing/)。

## setup() / loop(dt) と時刻表示

```lua
function setup()
    pico.log("hello.lua: setup()実行")
end

function loop(dt)
    elapsed_ms = elapsed_ms + dt
    local sec = math.floor(elapsed_ms / 1000)
    if sec ~= last_shown_sec then
        last_shown_sec = sec
        pico.set(elapsed_label, "text", "loop: " .. sec .. "s")
    end

    local t = pico.get_time()
    if t.sec ~= last_shown_time_sec then
        last_shown_time_sec = t.sec
        pico.set(time_label, "text", string.format("%02d:%02d:%02d", t.hour, t.min, t.sec))
    end
end
```

`pico.get_time()` は安く呼べますが、表示の更新自体は「秒が変わったときだけ」に間引いています(毎フレーム同じ文字列を `pico.set()` しても再描画は起きませんが、計算自体を省略できます)。

## Checkboxの固有イベント

```lua
local checkbox = pico.create("Checkbox")
pico.on(checkbox, "checked_changed", function(id)
    pico.set(check_label, "text", "checked: " .. tostring(pico.get(id, "checked")))
end)
```

値そのものはコールバック引数で渡らず、`pico.get(id, "checked")` で読みます。関連: [イベント](../../guide/events/)。

## 複数画面への遷移(hello_sub.lua / hello_sub2.lua)

「サブ画面へ」ボタンから `pico.push_scene("/lua/hello_sub.lua")` で別画面を開きます。その画面には「電卓を開く」(`pico.launch_app("電卓")`)と「置き換えへ」(`pico.change_scene("/lua/hello_sub2.lua")`)の2つのボタンがあります。

```
ランチャ --push_scene--> hello.lua --push_scene--> hello_sub.lua --change_scene--> hello_sub2.lua
```

`hello_sub2.lua` から `pico.pop()` すると、`change_scene` で消費された `hello_sub.lua` を飛び越して、直接 `hello.lua` へ戻ります。この一連の遷移の仕組みは [シーン制御](../../guide/scenes/) で解説しています。
