-- ---- pico.async: コルーチンで「待つ」を直列に書く ----
--   local async = require("pico.async")
--   async.run(function()
--       async.sleep(500)
--       local ok, status, body = async.http("GET", url)
--       if async.message("続けますか?", "いいえ", "はい") then ... end
--   end)

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
