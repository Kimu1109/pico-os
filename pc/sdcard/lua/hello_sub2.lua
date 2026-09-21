-- pico.change_scene()で開かれる画面。hello_sub.luaを置き換える形で入る
-- (SceneFunctions::Changeなのでシーンスタックは伸びない)。
-- そのため、ここでpico.pop()すると hello_sub.lua を飛び越して
-- hello.luaへ直接戻る。

local x, y = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "font_size", 2)
pico.set(title, "text", "置き換え先")

local desc = pico.create("Label")
pico.set(desc, "x", x + margin)
pico.set(desc, "y", y + margin + 40)
pico.set(desc, "text", "pico.change_scene()で来た画面")

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 80)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 30)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    -- change_sceneで来ているのでスタックに積まれているのはhello.luaのまま。
    -- ここでpop()すると、hello_sub.luaを飛び越してhello.luaへ戻る
    pico.pop()
end)

pico.log("hello_sub2.lua: 起動しました")
