#pragma once

#include <cstring>

// require("pico.ui") / require("pico.async") で読める、OSに同梱のLuaモジュール。
// アプリのフォルダのファイルより優先する(名前が"pico."で始まるものはここだけ)。
// ソースはフラッシュに置くだけで、require されたときにだけコンパイルする(実行の外の浅い所で。
// LuaEngine::preloadModules)。
namespace LuaBuiltin {

    // ---- pico.ui: 宣言的なUIの組み立て ----
    //   local ui = require("pico.ui")
    //   local root, named = ui{ "LayoutContainer", w = 200, h = 120, direction = 0, children = {
    //       { "Label", text = "名前", name = "title" },
    //       { "Button", text = "OK", on_press_end = function(id) ... end },
    //   } }
    //   ui.Button{ text = "OK" }   -- ui.種類{...} でも書ける
    static const char* const kUi = R"LUA(
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
)LUA";

    // ---- pico.async: コルーチンで「待つ」を直列に書く ----
    //   local async = require("pico.async")
    //   async.run(function()
    //       async.sleep(500)
    //       local ok, status, body = async.http("GET", url)
    //       if async.message("続けますか?", "いいえ", "はい") then ... end
    //   end)
    static const char* const kAsync = R"LUA(
local M = {}

local function step(co, ...)
    local ok, err = coroutine.resume(co, ...)
    if not ok then pico.show_error(tostring(err)) end
end

function M.run(fn, ...)
    local co = coroutine.create(fn)
    step(co, ...)
    return co
end

-- starter(resume) を呼び、resume(...) が呼ばれるまで待つ。resumeに渡した値がawaitの戻り値になる。
-- starterの中で同期的にresumeを呼んでもよい
function M.await(starter)
    local co, ismain = coroutine.running()
    if ismain then error("async.await は async.run の中で使います", 2) end
    local done, waiting, pending = false, false, nil
    local function resume(...)
        if done then return end
        done = true
        if waiting then step(co, ...) else pending = table.pack(...) end
    end
    starter(resume)
    if pending then return table.unpack(pending, 1, pending.n) end
    waiting = true
    return coroutine.yield()
end

function M.sleep(ms)
    return M.await(function(resume) pico.after(ms, function() resume() end) end)
end

-- ok, status, body, err, headers, info を返す(pico.http_request のコールバックの引数と同じ)
function M.http(method, url, body, content_type, opts)
    return M.await(function(resume)
        local id = pico.http_request(method, url, body, content_type, function(...) resume(...) end, opts)
        if not id then resume(false, 0, nil, "リクエストを始められませんでした") end
    end)
end

local function dialog(make)
    return M.await(function(resume)
        local id = make()
        pico.on(id, "closed", function(_, is_ok, value) resume(is_ok, value) end)
    end)
end

-- 戻り値: is_ok(true=決定側)
function M.message(text, cancel_text, ok_text)
    return (dialog(function() return pico.show_message(text, cancel_text or "", ok_text or "OK") end))
end
-- 戻り値: 入力した文字列 | nil(キャンセル)
function M.input(label, initial, single_line)
    local ok, v = dialog(function() return pico.show_input(label, initial or "", single_line ~= false) end)
    if ok then return v end
end
-- 戻り値: 選んだ番号(0始まり) | nil
function M.choice(title, items, cancel_text)
    local ok, v = dialog(function() return pico.show_choice(title, items, cancel_text) end)
    if ok then return v end
end
-- 戻り値: "YYYY-MM-DD" | nil
function M.date(title, year, month, day)
    local ok, v = dialog(function() return pico.show_date(title, year, month, day) end)
    if ok then return v end
end
-- 戻り値: "HH:MM:SS" | nil
function M.time(title, hour, minute, second)
    local ok, v = dialog(function() return pico.show_time(title, hour, minute, second or 0) end)
    if ok then return v end
end
-- 戻り値: 数値 | nil
function M.number(title, initial)
    local ok, v = dialog(function() return pico.show_number(title, initial) end)
    if ok then return tonumber(v) end
end
function M.file_select(dir)
    local ok, v = dialog(function() return pico.show_file_select(dir) end)
    if ok then return v end
end
function M.file_save(dir, name)
    local ok, v = dialog(function() return pico.show_file_save(dir, name) end)
    if ok then return v end
end
function M.color()
    local ok, v = dialog(function() return pico.show_color() end)
    if ok then return v end
end

return M
)LUA";


    // ---- pico.tween: 値を一定時間かけて変えていく ----
    //   local tween = require("pico.tween")
    //   local h = tween.start{ from = 0, to = 100, duration = 500, ease = "ease_out",
    //       on_update = function(v) pico.set(bar, "value", v) end, on_done = function() end }
    //   tween.cancel(h)
    static const char* const kTween = R"LUA(
local M = {}

local EASE = {
    linear      = function(t) return t end,
    ease_in     = function(t) return t * t end,
    ease_out    = function(t) return 1 - (1 - t) * (1 - t) end,
    ease_in_out = function(t) if t < 0.5 then return 2 * t * t end return 1 - 2 * (1 - t) * (1 - t) end,
}
M.ease = EASE

-- opts: duration(ms) / from / to / ease("linear"|"ease_in"|"ease_out"|"ease_in_out"または関数) /
--       interval(ms。既定40) / on_update(value) / on_done()
function M.start(opts)
    local duration = math.max(1, opts.duration or 300)
    local from, to = opts.from or 0, opts.to or 1
    local ease = opts.ease or "linear"
    if type(ease) == "string" then ease = EASE[ease] or error("pico.tween: 未知のease '" .. ease .. "'", 2) end
    local interval = math.max(10, opts.interval or 40)
    local started = pico.millis()
    local h
    local function step(final)
        local p = final and 1 or math.min(1, (pico.millis() - started) / duration)
        if opts.on_update then opts.on_update(from + (to - from) * ease(p)) end
        if p >= 1 then
            if h then pico.cancel(h) end
            if opts.on_done then opts.on_done() end
        end
    end
    h = pico.every(interval, function() step(false) end)
    if opts.on_update then opts.on_update(from) end
    return h
end

function M.cancel(h) pico.cancel(h) end

return M
)LUA";

    inline const char* Source(const char* name) {
        if (!name) return nullptr;
        if (strcmp(name, "pico.ui") == 0) return kUi;
        if (strcmp(name, "pico.async") == 0) return kAsync;
        if (strcmp(name, "pico.tween") == 0) return kTween;
        return nullptr;
    }
}
