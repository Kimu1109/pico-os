-- SDスキャン式Luaアプリのデモ。
-- src/lua/LuaAppScanner.cpp が起動時に "/lua/apps/" 配下のサブディレクトリを
-- 走査し、main.lua があるものをディレクトリ名のままランチャのタイルへ登録する。
-- このファイルはApp_List.cppに一切手を加えることなくランチャに現れる。

local x, y, w, h = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "font_size", 2)
pico.set(title, "text", "スキャン確認")

local info = pico.create("Label")
pico.set(info, "x", x + margin)
pico.set(info, "y", y + margin + 40)
pico.set(info, "text", "/lua/apps/から自動登録されました")

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 80)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 30)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)
