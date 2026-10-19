-- ---- pico.ui: 宣言的なUIの組み立て ----
--   local ui = require("pico.ui")
--   local root, named = ui{ "LayoutContainer", w = 200, h = 120, direction = 0, children = {
--       { "Label", text = "名前", name = "title" },
--       { "Button", text = "OK", on_press_end = function(id) ... end },
--   } }
--   ui.Button{ text = "OK" }   -- ui.種類{...} でも書ける

local M = {}

-- 先に決めておく必要があるもの(項目がないのに選択はできない、など)
local PRE  = { min_value = true, max_value = true }
local POST = { value = true, selected_index = true, tab_selected = true, checked = true }
local SKIP = { type = true, children = true, name = true, items = true, tabs = true }

local function build(spec, named)
    local kind = spec[1] or spec.type
    if type(kind) ~= "string" then error("pico.ui: ウィジェットの種類がありません", 3) end
    local id = pico.create(kind)
    if spec.items then
        for _, it in ipairs(spec.items) do
            if type(it) == "table" then pico.list_add(id, it[1] or it.text or "", it)
            else pico.list_add(id, tostring(it)) end
        end
    end
    if spec.tabs then
        for _, t in ipairs(spec.tabs) do pico.tab_add(id, tostring(t)) end
    end
    local pre, mid, post = {}, {}, {}
    for k, v in pairs(spec) do
        if type(k) == "string" and not SKIP[k] then
            if k:sub(1, 3) == "on_" then
                pico.on(id, k:sub(4), v)
            elseif PRE[k] then pre[#pre + 1] = k
            elseif POST[k] then post[#post + 1] = k
            else mid[#mid + 1] = k end
        end
    end
    for _, k in ipairs(pre) do pico.set(id, k, spec[k]) end
    for _, k in ipairs(mid) do pico.set(id, k, spec[k]) end
    for _, k in ipairs(post) do pico.set(id, k, spec[k]) end
    if spec.name then
        pico.set_name(id, spec.name)
        named[spec.name] = id
    end
    if spec.children then
        for _, c in ipairs(spec.children) do
            pico.add_child(id, build(c, named))
        end
    end
    return id
end

local function make(spec)
    local named = {}
    local id = build(spec, named)
    return id, named
end

return setmetatable(M, {
    __call = function(_, spec) return make(spec) end,
    __index = function(_, kind)
        return function(spec)
            spec = spec or {}
            spec[1] = kind
            return make(spec)
        end
    end,
})
