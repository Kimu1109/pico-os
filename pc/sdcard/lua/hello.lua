-- pico-os Luaアプリのサンプル。
-- LuaScene(src/gui/scenes/LuaScene.hpp)がSDから読み込んで実行する。
-- pico.create/set/get/on/add_child/popという最小限のAPIだけで
-- 「ボタンを押すと数字が増える画面 + ランチャへ戻るボタン」を組み立てる。

local x, y, w, h = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "font_size", 2) -- FontFn::Big(32px)
pico.set(title, "text", "Lua Hello")

local count = 0
local count_label = pico.create("Label")
pico.set(count_label, "x", x + margin)
pico.set(count_label, "y", y + margin + 40)
pico.set(count_label, "text", "count: 0")

local inc_button = pico.create("Button")
pico.set(inc_button, "x", x + margin)
pico.set(inc_button, "y", y + margin + 70)
pico.set(inc_button, "w", 80)
pico.set(inc_button, "h", 30)
pico.set(inc_button, "text", "+1")
pico.on(inc_button, "press_start", function()
    count = count + 1
    pico.set(count_label, "text", "count: " .. count)
end)

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 120)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 30)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)

pico.log("hello.lua: 起動しました")
