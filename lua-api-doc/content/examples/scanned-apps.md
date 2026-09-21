---
title: "SD自動登録アプリ"
weight: 20
description: "pc/sdcard/lua/apps/スキャン確認/main.lua — C++側を一切変更せずランチャへ現れるアプリ"
---

`/lua/apps/<アプリ名>/main.lua` という構成でSDへ置くだけで、起動時のスキャン(`LuaAppScanner`)がランチャへタイルを自動で追加します。`src/functions/App_List.cpp` は一切変更していません。詳細は [はじめに](../../getting-started/) を参照してください。

このサンプルは自動登録の実演を兼ねて、`TabBar` / `DropdownMenu` / `Textbox` の固有イベントと、コンテナへの動的な出し入れをまとめて確認できる内容になっています。

## TabBar: tab_add + tab_changed

```lua
local tabbar = pico.create("TabBar")
pico.tab_add(tabbar, "A")
pico.tab_add(tabbar, "B")
pico.tab_add(tabbar, "C")

pico.on(tabbar, "tab_changed", function(id)
    pico.set(tab_label, "text", "tab: " .. pico.get(id, "tab_selected"))
end)
```

## DropdownMenu: list_add + dropdown_changed

```lua
local dropdown = pico.create("DropdownMenu")
pico.list_add(dropdown, "りんご")
pico.list_add(dropdown, "みかん")
pico.list_add(dropdown, "ぶどう")

pico.on(dropdown, "dropdown_changed", function(id)
    pico.set(dropdown_label, "text", "選択:" .. pico.get(id, "selected_index"))
end)
```

## Textbox: text_changed

```lua
local textbox = pico.create("Textbox")
pico.set(textbox, "is_single_line", true)
pico.set(textbox, "placeholder", "タップして入力")

pico.on(textbox, "text_changed", function(id)
    pico.set(textbox_label, "text", "text: " .. pico.get(id, "text"))
end)
```

オンスクリーンキーボードを閉じて確定した瞬間に一度だけ発火します(1文字ごとには発火しません)。

## add_child / remove_child でトグルする

```lua
local container = pico.create("LayoutContainer")

local dynamic_child = nil
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
```

「取り外してから破棄する」のが定番の流れです。取り外さずにいきなり `pico.destroy()` しても構いませんが(コンテナ側が破棄済みの子を掃除します)、取り外した子を**別のコンテナへ移す**、**位置を変えて独立したウィジェットとして使い続ける**といった用途では `pico.remove_child()` が必要になります。関連: [レイアウトコンテナ](../../guide/containers/)。
