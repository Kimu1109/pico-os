-- pico.push_scene()で開かれるサブ画面。hello.luaの「サブ画面へ」から遷移する。
-- pico.pop()で戻ればhello.luaが最初から実行し直される
-- (LuaSceneの仕様。Push()で退避されたLuaアプリの状態はスクリプト内のLua変数にあり
-- C++側からは見えないため、他のシーンのようにonExit()で退避してonEnter()で
-- 復元することができない)。

local x, y = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "font_size", 2)
pico.set(title, "text", "サブ画面")

local desc = pico.create("Label")
pico.set(desc, "x", x + margin)
pico.set(desc, "y", y + margin + 40)
pico.set(desc, "text", "pico.push_scene()で開いた画面")

-- pico.launch_app(): 登録簿(ランチャの全アプリ)を名前で引いて起動する。
-- Luaスクリプト同士に限らず、C++製の標準アプリへも直接ジャンプできる
local calc_button = pico.create("Button")
pico.set(calc_button, "x", x + margin)
pico.set(calc_button, "y", y + margin + 80)
pico.set(calc_button, "w", 140)
pico.set(calc_button, "h", 30)
pico.set(calc_button, "text", "電卓を開く")
pico.on(calc_button, "press_start", function()
    if not pico.launch_app("電卓") then
        pico.log("hello_sub.lua: 電卓が見つかりませんでした")
    end
end)

-- pico.change_scene(): push_scene と違いスタックを消費せずに置き換わる。
-- ここから戻る場合、次の画面(hello_sub2)からpico.pop()すると
-- このhello_sub.luaを飛び越してhello.luaへ直接戻る
local change_button = pico.create("Button")
pico.set(change_button, "x", x + margin)
pico.set(change_button, "y", y + margin + 120)
pico.set(change_button, "w", 140)
pico.set(change_button, "h", 30)
pico.set(change_button, "text", "置き換えへ")
pico.on(change_button, "press_start", function()
    pico.change_scene("/lua/hello_sub2.lua")
end)

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 160)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 30)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)

pico.log("hello_sub.lua: 起動しました")
