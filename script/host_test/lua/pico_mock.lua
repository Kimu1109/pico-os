-- pico.* のLua製の偽物。Luaアプリ(pc/sdcard/lua/apps/*/main.lua)のゲームの規則やUIの組み立てを、
-- 実機やPCビルドを動かさずに、普通のLua(lua_script_test)で確かめるための道具。
--
--   local mock = dofile(ROOT_DIR .. "/script/host_test/lua/pico_mock.lua")
--   mock.install()                       -- グローバルの pico を差し替える(戻り値は pico)
--   dofile(".../main.lua")               -- アプリを読み込む(setup()/loop(dt)は自分で呼ぶ)
--   mock.fire(id, "press_start", 10, 10) -- イベントを鳴らす(第3引数以降がコールバックの引数になる)
--   mock.advance(500)                    -- pico.after / pico.every を500ms進める
--   mock.widgets[id].text                -- ウィジェットの今のプロパティ
--   mock.find("name")                    -- pico.set_name で付けた名前から
--   mock.draws                           -- pico.draw_* の呼び出し回数
--   mock.sd[path] = "..."                -- 偽のSD(sd_read/sd_write/sd_exists/sd_list/store_*)
--
-- 本物と違う所: 描画はしない(数えるだけ)/ ウィジェットはプロパティの入れ物でしかない(レイアウトや
-- 当たり判定はしない)/ 音・HTTP・ダイアログは呼び出しを記録するだけ(mock.sounds / mock.requests / mock.dialogs)。
-- 本物の振る舞い(範囲外のエラーなど)は lua_engine_test / lua_ext_test が確かめている。
local M = {}

function M.install(opts)
    opts = opts or {}
    M.widgets = {}
    M.names = {}
    M.sd = {}
    M.draws = 0
    M.sounds = {}
    M.requests = {}
    M.dialogs = {}
    M.logs = {}
    M.popped = nil
    M.scene = nil
    M.touch = { x = 0, y = 0, down = false }
    M.pad = {}
    M.clock = 0
    M.timers = {}
    M.store = nil
    M.key_handler = nil
    M.back_handler = nil
    M.toasts = {}
    local next_id = 0
    local next_timer = 0

    local pico = {}
    M.pico = pico

    -- ---- ウィジェット ----
    function pico.create(kind)
        next_id = next_id + 1
        M.widgets[next_id] = { type = kind, x = 0, y = 0, w = 100, h = 100, visible = true, enabled = true,
                               callbacks = {}, children = {}, items = {}, tabs = {} }
        return next_id
    end
    local function w(id)
        local o = M.widgets[id]
        if not o then error("pico: 無効なID " .. tostring(id), 3) end
        return o
    end
    function pico.destroy(id)
        local o = M.widgets[id]
        if not o then return end
        if o.parent then
            local sib = M.widgets[o.parent].children
            for i, c in ipairs(sib) do if c == id then table.remove(sib, i) break end end
        end
        for name, nid in pairs(M.names) do if nid == id then M.names[name] = nil end end
        M.widgets[id] = nil
    end
    function pico.set(id, k, v) w(id)[k] = v end
    function pico.get(id, k) return w(id)[k] end
    function pico.on(id, ev, fn) w(id).callbacks[ev] = fn end
    function pico.off(id, ev)
        local had = w(id).callbacks[ev] ~= nil
        w(id).callbacks[ev] = nil
        return had
    end
    function pico.add_child(parent, child)
        w(parent); w(child)
        w(parent).children[#w(parent).children + 1] = child
        w(child).parent = parent
    end
    function pico.remove_child(parent, child)
        local sib = w(parent).children
        for i, c in ipairs(sib) do if c == child then table.remove(sib, i) end end
        w(child).parent = nil
    end
    function pico.parent(id) return w(id).parent end
    function pico.children(id) local out = {} for i, c in ipairs(w(id).children) do out[i] = c end return out end
    function pico.set_name(id, name) w(id); M.names[name] = id end
    function pico.find(name) return M.names[name] end
    function pico.get_rect(id) local o = w(id) return o.x, o.y, o.w, o.h end
    function pico.bring_to_front(id) w(id) return true end
    function pico.send_to_back(id) w(id) return true end
    function pico.text_set(id, text) w(id).text = text return true end
    function pico.set_dots(id, dots) w(id).dots = dots end
    function pico.scroll_to(id, child) w(id).scroll_child = child return true end
    function pico.show_keyboard(id) M.keyboard = id end
    function pico.hide_keyboard() M.keyboard = nil end
    function pico.invalidate() end
    function pico.mark_dirty() end

    -- ---- リスト・タブ ----
    function pico.list_add(id, text, o) local it = w(id).items it[#it + 1] = { text = text, color = o and o.color } w(id).item_count = #it end
    function pico.list_insert(id, i, text, o) table.insert(w(id).items, i + 1, { text = text, color = o and o.color }) w(id).item_count = #w(id).items end
    function pico.list_remove(id, i)
        local it = w(id).items
        if i < 0 or i >= #it then return false end
        table.remove(it, i + 1)
        w(id).item_count = #it
        return true
    end
    function pico.list_get(id, i) local it = w(id).items[i + 1] if it then return it.text, it.color end end
    function pico.list_clear(id) w(id).items = {} w(id).item_count = 0 end
    function pico.list_select(id, i) w(id).selected_index = i end
    function pico.list_scroll_to(id, i) w(id).scroll_index = i end
    function pico.tab_add(id, label) local t = w(id).tabs t[#t + 1] = label w(id).tab_count = #t return true end
    function pico.tab_label(id, i) return w(id).tabs[i + 1] end
    function pico.tab_set_label(id, i, label) if w(id).tabs[i + 1] then w(id).tabs[i + 1] = label return true end return false end
    function pico.tab_remove(id, i) if w(id).tabs[i + 1] then table.remove(w(id).tabs, i + 1) w(id).tab_count = #w(id).tabs return true end return false end
    function pico.tab_clear(id) w(id).tabs = {} w(id).tab_count = 0 end
    function pico.tab_link(tab, i, target) local l = w(tab).links or {} w(tab).links = l l[#l + 1] = { i, target } end
    function pico.tab_unlink(tab) w(tab).links = nil end

    -- ---- 描画(数えるだけ) ----
    for _, n in ipairs { "draw_pixel", "draw_line", "draw_rect", "fill_rect", "draw_circle", "fill_circle", "draw_ellipse",
                         "fill_ellipse", "draw_triangle", "fill_triangle", "draw_polygon", "fill_polygon", "draw_arc",
                         "fill_arc", "clear_rect", "draw_text", "draw_image", "draw_image_part", "draw_image_ex",
                         "draw_text_wrapped" } do
        pico[n] = function() M.draws = M.draws + 1 end
    end
    function pico.text_width(s) return #s * 8 end
    function pico.measure_text(s) return 1, 16 end
    function pico.get_draw_area() return 0, 0, 0, 0 end
    function pico.set_draw_area() end
    function pico.clear_draw_area() end
    function pico.get_pixel() return 15 end
    function pico.image_load() return 1 end
    function pico.image_create() return 1 end
    function pico.image_size() return 16, 16 end
    function pico.image_free() end
    function pico.image_target() end
    function pico.image_clear() end

    -- ---- 画面・入力 ----
    function pico.content_rect() return 0, 20, 240, 300 end
    function pico.pop(result) M.popped = { result = result } end
    function pico.push_scene(path, args) M.scene = { kind = "push", path = path, args = args } end
    function pico.change_scene(path, args) M.scene = { kind = "change", path = path, args = args } end
    function pico.launch_app(name) M.scene = { kind = "app", name = name } return true end
    function pico.args() return opts.args end
    function pico.get_touch() return M.touch.x, M.touch.y, M.touch.down end
    function pico.pad_connected() return opts.pad_connected ~= false end
    function pico.pad_down(n) return M.pad[n] or false end
    function pico.pad_pressed(n) return M.pad_pressed and M.pad_pressed[n] or false end
    function pico.pad_released() return false end
    function pico.on_key(fn) M.key_handler = fn end
    function pico.on_back(fn) M.back_handler = fn end
    function pico.go_back() if M.back_handler and M.back_handler() ~= false then return end pico.pop() end

    -- ---- 時間・タイマー ----
    function pico.millis() return M.clock end
    function pico.time() return 1790000000 + M.clock // 1000 end
    function pico.get_time() return { year = 2026, month = 10, day = 5, hour = 12, min = 0, sec = 0, wday = 1 } end
    function pico.after(ms, fn)
        next_timer = next_timer + 1
        M.timers[next_timer] = { due = M.clock + ms, fn = fn }
        return next_timer
    end
    function pico.every(ms, fn)
        next_timer = next_timer + 1
        M.timers[next_timer] = { due = M.clock + ms, fn = fn, every = ms }
        return next_timer
    end
    function pico.cancel(h) M.timers[h] = nil end

    -- ---- SD・設定・保存 ----
    function pico.sd_exists(p) return M.sd[p] ~= nil end
    function pico.sd_read(p) return M.sd[p] end
    function pico.sd_write(p, s, append) M.sd[p] = (append and (M.sd[p] or "") or "") .. s return true end
    function pico.sd_remove(p) M.sd[p] = nil return true end
    function pico.sd_mkdir() return true end
    function pico.sd_list(dir)
        local out = {}
        dir = dir:gsub("/$", "")
        for p, s in pairs(M.sd) do
            local name = p:match("^" .. dir:gsub("%p", "%%%0") .. "/([^/]+)$")
            if name then out[#out + 1] = { name = name, is_dir = false, size = #s } end
        end
        table.sort(out, function(a, b) return a.name < b.name end)
        return out
    end
    function pico.app_dir() return opts.app_dir or "/lua/apps/test" end
    function pico.path_join(...)
        local parts = { ... }
        local s = ""
        for _, p in ipairs(parts) do
            if p:sub(1, 1) == "/" then s = p elseif s == "" or s:sub(-1) == "/" then s = s .. p else s = s .. "/" .. p end
        end
        return s
    end
    local settings = {}
    function pico.settings_get(k, default) local v = settings[k] if v == nil then return default end return v end
    function pico.settings_set(k, v) settings[k] = tostring(v) return true end
    function pico.settings_all() local o = {} for k, v in pairs(settings) do o[k] = v end return o end
    function pico.store_load() return M.store end
    function pico.store_save(t) M.store = t return true end

    -- ---- ダイアログ・通知(記録するだけ。mock.close_dialog で結果を返せる) ----
    local function dialog(kind, ...)
        local id = pico.create("Dialog")
        w(id).dialog = kind
        w(id).args = { ... }
        M.dialogs[#M.dialogs + 1] = id
        return id
    end
    for _, k in ipairs { "message", "input", "file_save", "file_select", "color", "choice", "date", "time", "number", "progress" } do
        pico["show_" .. k] = function(...) return dialog(k, ...) end
    end
    function pico.toast(text) M.toasts[#M.toasts + 1] = text return true end
    function pico.log(s) M.logs[#M.logs + 1] = tostring(s) end
    function pico.show_error(s) M.logs[#M.logs + 1] = "ERROR: " .. tostring(s) end

    -- ---- 通信・音 ----
    function pico.http_request(method, url, body, ctype, cb, o)
        local r = { method = method, url = url, body = body, content_type = ctype, cb = cb, opts = o }
        M.requests[#M.requests + 1] = r
        return #M.requests
    end
    function pico.http_cancel() end
    function pico.sound_play(...) M.sounds[#M.sounds + 1] = { ... } return true end
    function pico.sound_stop() end
    function pico.sound_available() return true end
    function pico.beep(...) M.sounds[#M.sounds + 1] = { ... } end
    function pico.music_play() return true end
    function pico.music_play_text() return true end
    function pico.music_stop() end
    function pico.note_freq() return 440 end

    -- ---- 暗号・変換(本物ほど強くはない。往復できるだけ) ----
    function pico.encrypt(s, pw) return "enc2:" .. (pw or "") .. ":" .. s end
    function pico.decrypt(s, pw)
        local p, body = s:match("^enc2:(.-):(.*)$")
        if not p then return nil, "bad" end
        if p ~= (pw or "") then return nil, "auth" end
        return body
    end
    function pico.is_encrypted(s) return s:sub(1, 5) == "enc2:" end
    function pico.hash(s) return string.format("%064x", #s) end
    function pico.random_bytes(n) return string.rep("\4", n) end

    -- JSON(最小限。テーブルをそのまま往復するだけ)
    pico.json_null = {}
    function pico.json_encode(v) M.json_last = v return "<json>" end
    function pico.json_decode() return M.json_last end

    -- require: テストでは普通のLuaのrequire/dofileを使うので何もしない
    pico.memory_info = function() return { lua_used = 1, lua_budget = 2, lua_free = 1, heap_free = 1, heap_used = 1, image_bytes = 0 } end
    pico.wifi_status = function() return { connected = true, status = "connected", enabled = true, ssid = "test" } end
    pico.url_encode = function(s) return (s:gsub("[^%w%-_%.~]", function(c) return string.format("%%%02X", c:byte()) end)) end
    pico.url_decode = function(s) return (s:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end)) end

    _G.pico = pico
    return pico
end

-- ---- テストから使う操作 ----
function M.fire(id, event, ...)
    local o = M.widgets[id]
    assert(o, "mock.fire: 無効なID")
    local fn = o.callbacks[event]
    if fn then return fn(id, ...) end
end

function M.find(name) return M.names[name] end

-- 時間を進めて、時間になったタイマーを呼ぶ
function M.advance(ms)
    local target = M.clock + ms
    while true do
        local best, best_h
        for h, t in pairs(M.timers) do
            if t.due <= target and (not best or t.due < best.due) then best, best_h = t, h end
        end
        if not best then break end
        M.clock = best.due
        if best.every then best.due = best.due + best.every else M.timers[best_h] = nil end
        best.fn(best_h)
    end
    M.clock = target
end

-- 記録したダイアログを閉じて、closedのコールバックを呼ぶ
function M.close_dialog(id, is_ok, value)
    M.fire(id, "closed", is_ok, value)
    pico.destroy(id)
end

-- 記録したHTTPリクエストへ応答を返す
function M.respond(index, ok, status, body, err, headers)
    local r = M.requests[index]
    assert(r, "mock.respond: そのリクエストは無い")
    return r.cb(ok, status, body, err, headers or {}, { size = body and #body or 0 })
end

return M
