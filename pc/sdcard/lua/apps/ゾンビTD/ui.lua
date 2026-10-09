-- ゾンビTD: 下の欄(情報の行 + ボタンの行)。
--   情報の行: 選んだ兵士/建物の情報と[強化][修理][売却]、建てる所の案内、準備時間の次のウェーブの予告
--   ボタン: [戻る][雇う][建設][選択](範囲選択)[x1/x3/停止](速さ)[他]
local G = require("state")
local PAL = require("palette")
local orders = require("orders")
local soldiers = require("soldiers")
local buildings = require("buildings")
local waves = require("waves")
local M = {}

local CX, PY, CW, INFO_H, BTN_H         -- 置き場所(init で決まる)
local actions                           -- game.lua の操作 {back=, center=, new_game=}
local held = nil

function M.box(x, y, w, h, label, on)
    pico.fill_rect(x, y, w, h, on and PAL.mid or PAL.light)
    pico.draw_rect(x, y, w, h, 0)
    if label then pico.draw_text(x + w // 2, y + (h - 16) // 2, label, 0, 0, "center") end
end
local box = M.box

local BUTTONS = {
    { id = "back", label = "戻る" }, { id = "hire", label = "雇う" }, { id = "build", label = "建設" },
    { id = "select", label = "選択" }, { id = "speed" }, { id = "more", label = "他" },
}
local SOLDIER_BUTTONS, BUILDING_BUTTONS
M.buttons = BUTTONS

local function info_buttons()
    if G.info_msg or G.build_mode then return nil end
    if G.bsel then return BUILDING_BUTTONS end
    if #G.sel == 1 then return SOLDIER_BUTTONS end
end

local function info_label(b)
    local bs = G.bsel
    if bs then
        local v
        if b.id == "upgrade" then v = buildings.upgrade_cost(bs)
        elseif b.id == "repair" then v = buildings.repair_cost(bs)
        else v = buildings.sell_value(bs) end
        local head = b.id == "upgrade" and "強" or (b.id == "repair" and "修" or "売")
        return v and (head .. v) or "-"
    end
    local s = G.sel[1]
    if b.id == "upgrade" then
        local c = soldiers.upgrade_cost(s)
        return c and ("強化" .. c) or "最大"
    end
    return "売" .. soldiers.sell_value(s)
end

-- 建物の情報の行: 名前とレベル・耐久(建設中/強化中/修理中は残り秒。耐久は頭の上のバーで見える)
local function building_text(b)
    local left = buildings.remaining(b)
    if left then
        return string.format("%s %s%d秒", b.t.name, b.state == "build" and "建設" or (b.state == "up" and "強化" or "修理"),
            math.ceil(left))
    end
    return string.format("%s%d %d/%d", b.t.name, b.lv, math.max(0, math.floor(b.hp)), buildings.max_hp(b))
end

local function render()
    pico.fill_rect(CX, PY, CW, INFO_H + BTN_H, PAL.dark)
    local buttons = info_buttons()
    if G.info_msg then
        pico.draw_text(CX + 4, PY + 2, G.info_msg, 14, 0)
    elseif G.build_mode then
        local t = buildings.TYPES[G.build_mode]
        pico.draw_text(CX + 4, PY + 2, string.format("%s($%d) 置く所をタップ", t.name, t.cost), 14, 0)
    elseif G.bsel then
        pico.draw_text(CX + 2, PY + 2, building_text(G.bsel), 15, 0)
    elseif #G.sel == 1 then
        local s = G.sel[1]
        pico.draw_text(CX + 2, PY + 2, string.format("%sLv%d %d/%d", s.t.name, s.lv, math.max(0, s.hp), soldiers.max_hp(s)), 15, 0)
    elseif #G.sel > 1 then
        pico.draw_text(CX + 4, PY + 2, #G.sel .. "人選択中 タップで移動", 15, 0)
    elseif G.select_mode then
        pico.draw_text(CX + 4, PY + 2, "ドラッグで囲んで選ぶ", 14, 0)
    elseif G.mode == "play" and waves.phase == "prep" then
        pico.draw_text(CX + 4, PY + 2, waves.preview(waves.n), 14, 0)
    else
        pico.draw_text(CX + 4, PY + 2, string.format("兵士%d人 タップで選ぶ", soldiers.count()), 8, 0)
    end
    for _, b in ipairs(buttons or {}) do
        box(CX + b.x, PY + 1, b.w - 2, INFO_H - 2, nil, held == b)
        pico.draw_text(CX + b.x + (b.w - 2) // 2, PY + 2, info_label(b), 0, 0, "center")
    end
    for _, b in ipairs(BUTTONS) do
        local label = b.label or (G.speed == 0 and "停止" or ("x" .. G.speed))
        box(CX + b.x + 1, PY + INFO_H + 2, b.w - 2, BTN_H - 4, label, held == b or (b.id == "select" and G.select_mode))
    end
end

local MORE = { "ベースを見る", "選択を解除", "新しく始める" }

-- 選ぶ一覧を出して、選んだ番号(1始まり)で fn を呼ぶ
local function choose(title, items, fn)
    local d = pico.show_choice(title, items)
    pico.on(d, "closed", function(_, ok, idx) if ok and idx then fn(idx + 1) end end)
end

function M.press(b)
    local id = b.id
    if id == "back" then actions.back()
    elseif id == "hire" or id == "build" then
        if G.mode ~= "play" then return end
        if G.build_mode then G.build_mode = false; pico.invalidate(G.panel); return end
        local mod = id == "hire" and soldiers or buildings
        local items = {}
        for i, k in ipairs(mod.KINDS) do
            local t = mod.TYPES[k]
            items[i] = string.format("%s%s $%d", t.name, id == "hire" and "兵" or "", t.cost)
        end
        choose(string.format("%s($%d)", id == "hire" and "雇う" or "建てる", G.money), items, function(i)
            if id == "hire" then orders.hire(mod.KINDS[i])
            else orders.deselect_all(); G.build_mode = mod.KINDS[i] end
            pico.invalidate(G.panel)
        end)
    elseif id == "select" then G.select_mode = not G.select_mode
    elseif id == "speed" then G.speed = G.speed == 1 and 3 or (G.speed == 3 and 0 or 1)
    elseif id == "more" then
        choose("その他", MORE, function(i)
            if i == 1 then actions.center()
            elseif i == 2 then orders.deselect_all()
            else
                local d = pico.show_message("今のゲームをやめて、新しいマップで始めますか?", "やめる", "始める")
                pico.on(d, "closed", function(_, ok) if ok then actions.new_game() end end)
            end
        end)
    elseif id == "upgrade" then orders.upgrade()
    elseif id == "repair" then orders.repair()
    elseif id == "sell" then orders.sell()
    end
    pico.invalidate(G.panel)
end

local function button_at(lx, ly)
    if ly >= INFO_H then
        for _, b in ipairs(BUTTONS) do if lx >= b.x and lx < b.x + b.w then return b end end
    else
        for _, b in ipairs(info_buttons() or {}) do if lx >= b.x and lx < b.x + b.w then return b end end
    end
end

-- layout = {x=, y=, w=, info_h=, btn_h=}
function M.init(layout, acts)
    CX, PY, CW, INFO_H, BTN_H = layout.x, layout.y, layout.w, layout.info_h, layout.btn_h
    actions = acts
    local bw = CW // #BUTTONS
    for i, b in ipairs(BUTTONS) do b.x = (i - 1) * bw; b.w = (i == #BUTTONS) and (CW - b.x) or bw end
    SOLDIER_BUTTONS = { { id = "upgrade", x = CW - 96, w = 52 }, { id = "sell", x = CW - 42, w = 42 } }
    BUILDING_BUTTONS = { { id = "upgrade", x = CW - 120, w = 40 }, { id = "repair", x = CW - 80, w = 40 },
        { id = "sell", x = CW - 40, w = 40 } }
    local panel = G.panel
    pico.on(panel, "render", render)
    pico.on(panel, "press_start", function(_, _, _, lx, ly)
        held = button_at(lx, ly)
        pico.invalidate(panel)
    end)
    pico.on(panel, "press_end", function(_, _, _, lx, ly)
        local b = held
        held = nil
        pico.invalidate(panel)
        if b and b == button_at(lx, ly) then M.press(b) end
    end)
    pico.on(panel, "press_out", function() held = nil; pico.invalidate(panel) end)
end

return M
