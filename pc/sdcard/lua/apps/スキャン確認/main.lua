-- SDスキャン式Luaアプリのデモ。
-- src/lua/LuaAppScanner.cpp が起動時に "/lua/apps/" 配下のサブディレクトリを
-- 走査し、main.lua があるものをディレクトリ名のままランチャのタイルへ登録する。
-- このファイルはApp_List.cppに一切手を加えることなくランチャに現れる。
--
-- あわせて、Lua APIの細部の穴埋め(2026-09-21)で追加したAPIのデモも兼ねる:
--   pico.tab_add / "tab_changed"          … TabBar
--   pico.list_add / "dropdown_changed"    … DropdownMenu
--   "text_changed"                        … Textbox
--   pico.add_child / pico.remove_child    … LayoutContainer

local x, y, w, h = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "font_size", 1)
pico.set(title, "text", "スキャン確認 / API穴埋め検証")

-- TabBar: pico.tab_add()で3タブ追加し、tab_changedで選択中indexを表示
local tabbar = pico.create("TabBar")
pico.set(tabbar, "x", x + margin)
pico.set(tabbar, "y", y + margin + 30)
pico.set(tabbar, "w", w - margin * 2)
pico.set(tabbar, "h", 24)
pico.tab_add(tabbar, "A")
pico.tab_add(tabbar, "B")
pico.tab_add(tabbar, "C")

local tab_label = pico.create("Label")
pico.set(tab_label, "x", x + margin)
pico.set(tab_label, "y", y + margin + 58)
pico.set(tab_label, "text", "tab: 0")
pico.on(tabbar, "tab_changed", function(id)
    pico.set(tab_label, "text", "tab: " .. pico.get(id, "tab_selected"))
end)

-- DropdownMenu: pico.list_add()で3項目追加し、dropdown_changedで選択indexを表示
local dropdown = pico.create("DropdownMenu")
pico.set(dropdown, "x", x + margin)
pico.set(dropdown, "y", y + margin + 82)
pico.set(dropdown, "w", 120)
pico.list_add(dropdown, "りんご")
pico.list_add(dropdown, "みかん")
pico.list_add(dropdown, "ぶどう")

local dropdown_label = pico.create("Label")
pico.set(dropdown_label, "x", x + margin + 130)
pico.set(dropdown_label, "y", y + margin + 90)
pico.set(dropdown_label, "text", "未選択")
pico.on(dropdown, "dropdown_changed", function(id)
    pico.set(dropdown_label, "text", "選択:" .. pico.get(id, "selected_index"))
end)

-- Textbox: text_changedで確定後の文字列を表示
local textbox = pico.create("Textbox")
pico.set(textbox, "x", x + margin)
pico.set(textbox, "y", y + margin + 122)
pico.set(textbox, "max_width", w - margin * 2)
pico.set(textbox, "max_height", 26)
pico.set(textbox, "is_single_line", true)
pico.set(textbox, "placeholder", "タップして入力")

local textbox_label = pico.create("Label")
pico.set(textbox_label, "x", x + margin)
pico.set(textbox_label, "y", y + margin + 152)
pico.set(textbox_label, "text", "text: (未入力)")
pico.on(textbox, "text_changed", function(id)
    pico.set(textbox_label, "text", "text: " .. pico.get(id, "text"))
end)

-- add_child/remove_child: 子を作って足し、もう一度押すと破棄せず外してdestroy
local container = pico.create("LayoutContainer")
pico.set(container, "x", x + margin)
pico.set(container, "y", y + margin + 178)
pico.set(container, "w", w - margin * 2)
pico.set(container, "h", 24)

local dynamic_child = nil
local toggle_button = pico.create("Button")
pico.set(toggle_button, "x", x + margin)
pico.set(toggle_button, "y", y + margin + 206)
pico.set(toggle_button, "w", 140)
pico.set(toggle_button, "h", 26)
pico.set(toggle_button, "text", "子を追加")
pico.on(toggle_button, "press_start", function()
    if dynamic_child == nil then
        dynamic_child = pico.create("Label")
        pico.set(dynamic_child, "text", "追加された子")
        pico.add_child(container, dynamic_child)
        pico.set(toggle_button, "text", "子を外す")
    else
        pico.remove_child(container, dynamic_child)
        pico.destroy(dynamic_child)
        dynamic_child = nil
        pico.set(toggle_button, "text", "子を追加")
    end
end)

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 238)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 26)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)
