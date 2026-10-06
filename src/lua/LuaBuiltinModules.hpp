#pragma once

#include <cstring>

// require("pico.ui") / require("pico.async") / require("pico.game") 等で読める、OSに同梱のLuaモジュール。
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


    // ---- pico.game: 2Dゲーム向けの簡易エンジン ----
    //   local game = require("pico.game")
    //   local g = game.new{ bg = 12, pad = true }
    //   local map = g:tilemap{ image = "tiles.pimg", tile = 16, solid = true,
    //       legend = { ["#"] = 1 }, rows = { "#....#", "######" } }
    //   local p = g:sprite{ image = "tiles.pimg", w = 16, h = 16, frame = 4, x = 16, y = 0,
    //       gravity = 900, solid = true }
    //   g:follow(p)
    //   function g:on_update(dt) p.vx = g:axis() * 80 end
    // キャンバス1枚に描き、動いたスプライトの周りだけを描き直す(カメラが動いたら全体)。
    // タイルの描画はC++(pico.draw_tilemap)。詳しくは lua-api-doc の api/game.md
    static const char* const kGame = R"LUA(
local M = {}
local floor, min, max = math.floor, math.min, math.max

local Game, Sprite, Tilemap = {}, {}, {}
Game.__index = Game
Sprite.__index = Sprite
Tilemap.__index = Tilemap

-- pico.pad_down 等が受け付けるボタン名(それ以外の名前の画面ボタンはパッドを見ない)
local PAD = { up = true, down = true, left = true, right = true, a = true, b = true, x = true, y = true,
              l = true, r = true, zl = true, zr = true, start = true, select = true, home = true }
local PAD_H = 56

local function round(v) return floor(v + 0.5) end

-- 当たり判定の箱(ワールド座標)
local function box(s)
    local hb = s.hitbox
    if hb then return s.x + hb[1], s.y + hb[2], hb[3], hb[4] end
    return s.x, s.y, s.w, s.h
end

function M.overlaps(a, b)
    local ax, ay, aw, ah = box(a)
    local bx, by, bw, bh = box(b)
    return ax < bx + bw and bx < ax + aw and ay < by + bh and by < ay + ah
end

-- ================= ゲーム =================

-- opts: x,y,w,h(画面座標。既定はcontent_rect全体) / bg(背景色。既定0) /
--       pad(trueで画面下に操作ボタン) / manual(trueなら自分でg:step(dt)を呼ぶ)
function M.new(o)
    o = o or {}
    local cx, cy, cw, ch = pico.content_rect()
    local g = setmetatable({}, Game)
    g.x, g.y = o.x or cx, o.y or cy
    g.w, g.h = o.w or (cx + cw - g.x), o.h or (cy + ch - g.y)
    g.vw, g.vh = g.w, g.h - (o.pad and PAD_H or 0)
    g.bg = o.bg or 0
    g.sprites, g.maps, g.images, g.rules, g.timers, g.states, g.buttons = {}, {}, {}, {}, {}, {}, {}
    g.cam_x, g.cam_y, g.time, g.paused = 0, 0, 0, false
    g.touch = { x = 0, y = 0, wx = 0, wy = 0, down = false, pressed = false, released = false }
    g._iw, g._held, g._latch, g._cur, g._prev, g._rects = {}, {}, {}, {}, {}, {}
    g._seq, g._full, g._tdown, g._tlatch = 0, true, false, false

    local c = pico.create("Canvas")
    g.canvas = c
    pico.set(c, "x", g.x); pico.set(c, "y", g.y)
    pico.set(c, "w", g.w); pico.set(c, "h", g.h)
    pico.set(c, "background_color", g.bg)
    pico.on(c, "render", function() g:_render() end)
    local function touch(kind, lx, ly)
        g:_touch(kind, lx or 0, ly or 0)
    end
    pico.on(c, "press_start", function(_, _, _, lx, ly) touch("start", lx, ly) end)
    pico.on(c, "press_move", function(_, _, _, lx, ly) touch("move", lx, ly) end)
    pico.on(c, "press_end", function(_, _, _, lx, ly) touch("end", lx, ly) end)
    pico.on(c, "press_out", function(_, _, _, lx, ly) touch("end", lx, ly) end)

    if o.pad then
        local bw, y0 = floor(g.w / 6), g.vh
        local names = { "left", "up", "down", "right", "b", "a" }
        for i, n in ipairs(names) do
            local w = (i == 6) and (g.w - bw * 5) or bw
            g:button{ name = n, x = (i - 1) * bw, y = y0, w = w, h = PAD_H }
        end
    end
    if not o.manual then
        g._timer = pico.every(1, function() g:step() end)
    end
    return g
end

-- 画像を読む(同じパスは1回だけ)。読めなければエラー
function Game:image(path)
    local h = self.images[path]
    if h then return h end
    h = pico.image_load(path)
    if not h then error("pico.game: 画像を読めません: " .. tostring(path), 2) end
    self.images[path] = h
    return h
end

function Game:_img(v)
    if type(v) == "string" then return self:image(v) end
    return v
end

-- ---- 時間 ----

function Game:step(dt)
    local now = pico.millis()
    if not dt then dt = self._last and (now - self._last) / 1000 or 0 end
    self._last = now
    if dt > 0.05 then dt = 0.05 end
    if dt < 0 then dt = 0 end
    self:_input()
    if not self.paused then
        self.time = self.time + dt
        self:_run_timers(dt)
        local st = self.cur_state
        if st and st.update then st.update(self, dt) end
        if self.on_update then self:on_update(dt) end
        local list = self.sprites
        for i = 1, #list do
            local s = list[i]
            if not s.dead then s:_update(dt) end
        end
        self:_check_rules()
        self:_follow()
    end
    self:_compact()
    self:_flush()
end

function Game:pause(p) self.paused = (p ~= false) end
-- 自動で回すのをやめる(g:step()を自分で呼べば動く)
function Game:stop()
    if self._timer then pico.cancel(self._timer); self._timer = nil end
end
-- 止めてキャンバスごと消す
function Game:destroy()
    self:stop()
    if self.canvas then pico.destroy(self.canvas); self.canvas = nil end
end

local function add_timer(g, sec, fn, every)
    local t = { due = g.time + sec, sec = sec, fn = fn, every = every }
    g.timers[#g.timers + 1] = t
    return t
end
function Game:after(sec, fn) return add_timer(self, sec, fn, false) end
function Game:every(sec, fn) return add_timer(self, max(sec, 0.001), fn, true) end
function Game:cancel(t) if t then t.dead = true end end

function Game:_run_timers()
    local list, i = self.timers, 1
    while i <= #list do
        local t = list[i]
        if not t.dead and self.time >= t.due then
            if t.every then t.due = max(t.due + t.sec, self.time) else t.dead = true end
            t.fn(self)
        end
        if t.dead then table.remove(list, i) else i = i + 1 end
    end
end

-- ---- 状態(タイトル/プレイ/ゲームオーバー等) ----

-- t: { enter = fn(g, ...), update = fn(g, dt), draw = fn(g, ox, oy), exit = fn(g) }
function Game:state(name, t) self.states[name] = t end

function Game:go(name, ...)
    local t = self.states[name]
    if not t then error("pico.game: 知らない状態です: " .. tostring(name), 2) end
    local cur = self.cur_state
    if cur and cur.exit then cur.exit(self) end
    self.cur_state, self.state_name = t, name
    self._full = true
    if t.enter then t.enter(self, ...) end
end

-- スプライト・タイルマップ・ゲーム内タイマーを全部消す(当たり判定の規則と状態は残す)
function Game:clear()
    for _, s in ipairs(self.sprites) do s.dead = true end
    self.maps, self.timers = {}, {}
    self.world_w, self.world_h, self._target = nil, nil, nil
    self.cam_x, self.cam_y = 0, 0
    self._full = true
end

-- ---- 入力 ----

function Game:_input()
    local held, latch, cur, prev = self._held, self._latch, self._cur, self._prev
    for k in pairs(prev) do prev[k] = nil end
    for k, v in pairs(cur) do prev[k] = v; cur[k] = nil end
    for k in pairs(held) do cur[k] = true end
    for k in pairs(latch) do cur[k] = true; latch[k] = nil end
    local t = self.touch
    local down = self._tdown or self._tlatch
    t.pressed = down and not t._was
    t.released = t._was and not down
    t.down = down
    t._was = down
    self._tlatch = false
    t.wx, t.wy = t.x + self.cam_x, t.y + self.cam_y
end

function Game:down(name)
    if self._cur[name] then return true end
    return PAD[name] and pico.pad_down(name) or false
end
function Game:pressed(name)
    if self._cur[name] and not self._prev[name] then return true end
    return PAD[name] and pico.pad_pressed(name) or false
end
function Game:released(name)
    if self._prev[name] and not self._cur[name] then return true end
    return PAD[name] and pico.pad_released(name) or false
end
-- -1/0/1 の左右と上下
function Game:axis()
    local h = (self:down("right") and 1 or 0) - (self:down("left") and 1 or 0)
    local v = (self:down("down") and 1 or 0) - (self:down("up") and 1 or 0)
    return h, v
end

-- 画面ボタン: { name=, x=, y=, w=, h=(キャンバスの中の座標), label= }
function Game:button(b)
    b.label = b.label or b.name
    b.x, b.y, b.w, b.h = floor(b.x), floor(b.y), floor(b.w), floor(b.h)
    self.buttons[#self.buttons + 1] = b
    self:dirty(b.x, b.y, b.w, b.h)
    return b
end

local function button_at(g, lx, ly)
    local bs = g.buttons
    for i = #bs, 1, -1 do
        local b = bs[i]
        if lx >= b.x and lx < b.x + b.w and ly >= b.y and ly < b.y + b.h then return b end
    end
end

function Game:_touch(kind, lx, ly)
    local old, nb = self._tbtn, nil
    local t = self.touch
    if kind == "end" then
        self._tdown = false
    else
        local b = button_at(self, lx, ly)
        if kind == "start" then
            if b then nb = b
            else
                self._tdown, self._tlatch = true, true
                t.x, t.y = lx, ly
            end
        elseif old then
            nb = b -- 押したまま隣のボタンへ滑らせると切り替わる
        elseif self._tdown then
            t.x, t.y = lx, ly
        end
    end
    self._tbtn = nb
    if old ~= nb then
        if old then self._held[old.name] = nil; self:dirty(old.x, old.y, old.w, old.h) end
        if nb then
            self._held[nb.name] = true
            self._latch[nb.name] = true
            self:dirty(nb.x, nb.y, nb.w, nb.h)
        end
    end
end

-- ---- スプライト ----

-- o: image(ハンドルかパス), x, y, w, h, frame(0始まり), anims={名前={frames={...}, fps=, loop=}},
--    vx, vy, gravity(px/秒²), max_fall, solid(タイルとぶつかる), bounded(ワールドの端で止まる),
--    hitbox={x,y,w,h}, layer, tag, color(画像が無いときの塗り色), flip_x, flip_y, visible,
--    on_update(s, dt), draw(s, sx, sy)
function Game:sprite(o)
    local s = setmetatable(o or {}, Sprite)
    s.game = self
    s.image = self:_img(s.image)
    if s.image and (not s.w or not s.h) then
        local iw, ih = pico.image_size(s.image)
        s.w, s.h = s.w or iw, s.h or ih
    end
    s.x, s.y, s.w, s.h = s.x or 0, s.y or 0, s.w or 8, s.h or 8
    s.vx, s.vy, s.frame, s.layer = s.vx or 0, s.vy or 0, s.frame or 0, s.layer or 0
    if s.visible == nil then s.visible = true end
    s.on_ground, s.hit_wall, s.hit_ceiling = false, 0, false
    self._seq = self._seq + 1
    s._seq = self._seq
    self.sprites[#self.sprites + 1] = s
    self._order = true
    if s.anim then local a = s.anim; s.anim = nil; s:play(a) end
    return s
end

function Sprite:remove() self.dead = true end

function Sprite:set_layer(n)
    self.layer = n
    self.game._order = true
end

function Sprite:play(name, restart)
    local a = self.anims and self.anims[name]
    if not a then error("pico.game: 知らないアニメーションです: " .. tostring(name), 2) end
    if self.anim == name and not restart then return end
    self.anim, self._a, self._ai, self._at, self.anim_done = name, a, 1, 0, false
    self.frame = a.frames[1]
end

function Sprite:stop_anim() self.anim, self._a = nil, nil end

function Sprite:overlaps(o) return M.overlaps(self, o) end

-- 画面上で中心が合うように置く
function Sprite:center() return self.x + self.w / 2, self.y + self.h / 2 end

-- 見た目が変わった(drawで描く中身を変えた等)ので描き直す
function Sprite:dirty() self._rv = nil end

function Sprite:_update(dt)
    local a = self._a
    if a and not self.anim_done then
        self._at = self._at + dt
        local spf = 1 / (a.fps or 8)
        while self._at >= spf do
            self._at = self._at - spf
            local i = self._ai + 1
            if i > #a.frames then
                if a.loop == false then
                    i = #a.frames
                    self.anim_done = true
                    if a.on_done then a.on_done(self) end
                    break
                end
                i = 1
            end
            self._ai = i
        end
        self.frame = a.frames[self._ai]
    end
    if self.on_update then self:on_update(dt) end
    if self.dead then return end
    if self.gravity then
        self.vy = self.vy + self.gravity * dt
        if self.max_fall and self.vy > self.max_fall then self.vy = self.max_fall end
    end
    self.on_ground, self.hit_wall, self.hit_ceiling = false, 0, false
    if self.vx ~= 0 or self.vy ~= 0 then self:move(self.vx * dt, self.vy * dt) end
end

-- (dx, dy)だけ動かす。solidならタイルで止まり、boundedならワールドの端で止まる。
-- 止まった向きは on_ground / hit_ceiling / hit_wall(-1=左, 1=右) に入る
function Sprite:move(dx, dy)
    local g = self.game
    if dx ~= 0 then
        self.x = self.x + dx
        if self.solid then g:_tiles(self, true, dx) end
    end
    if dy ~= 0 then
        self.y = self.y + dy
        if self.solid then g:_tiles(self, false, dy) end
    end
    if self.bounded then
        local ww, wh = g:world_size()
        local bx, by, bw, bh = box(self)
        if bx < 0 then self.x = self.x - bx; self.hit_wall = -1; self.vx = max(self.vx, 0)
        elseif bx + bw > ww then self.x = self.x - (bx + bw - ww); self.hit_wall = 1; self.vx = min(self.vx, 0) end
        if by < 0 then self.y = self.y - by; self.hit_ceiling = true; self.vy = max(self.vy, 0)
        elseif by + bh > wh then self.y = self.y - (by + bh - wh); self.on_ground = true; self.vy = min(self.vy, 0) end
    end
end

-- 動いた軸の向きにだけ、新しく入ったマスを近い順に調べて押し戻す
function Game:_tiles(s, horiz, d)
    for _, m in ipairs(self.maps) do
        if m._solid then
            local bx, by, bw, bh = box(s)
            local tw, th, mx, my = m.tw, m.th, m.x, m.y
            local e = 0.0001
            if horiz then
                local r0, r1 = floor((by - my) / th), floor((by + bh - e - my) / th)
                if d > 0 then
                    local from, to = floor((bx + bw - d - e - mx) / tw), floor((bx + bw - e - mx) / tw)
                    for c = from, to do
                        if m:_row_solid(c, r0, r1, true) then
                            s.x = s.x - (bx + bw - (mx + c * tw)); s.hit_wall = 1
                            if s.vx > 0 then s.vx = 0 end
                            break
                        end
                    end
                else
                    local from, to = floor((bx - d - mx) / tw), floor((bx - mx) / tw)
                    for c = from, to, -1 do
                        if m:_row_solid(c, r0, r1, true) then
                            s.x = s.x + (mx + (c + 1) * tw - bx); s.hit_wall = -1
                            if s.vx < 0 then s.vx = 0 end
                            break
                        end
                    end
                end
            else
                local c0, c1 = floor((bx - mx) / tw), floor((bx + bw - e - mx) / tw)
                if d > 0 then
                    local from, to = floor((by + bh - d - e - my) / th), floor((by + bh - e - my) / th)
                    for r = from, to do
                        if m:_row_solid(r, c0, c1, false) then
                            s.y = s.y - (by + bh - (my + r * th)); s.on_ground = true
                            if s.vy > 0 then s.vy = 0 end
                            break
                        end
                    end
                else
                    local from, to = floor((by - d - my) / th), floor((by - my) / th)
                    for r = from, to, -1 do
                        if m:_row_solid(r, c0, c1, false) then
                            s.y = s.y + (my + (r + 1) * th - by); s.hit_ceiling = true
                            if s.vy < 0 then s.vy = 0 end
                            break
                        end
                    end
                end
            end
        end
    end
end

-- ---- スプライト同士 ----

-- タグaのスプライトとタグbのスプライトが重なっている間、毎フレーム fn(sa, sb) を呼ぶ
function Game:collide(a, b, fn)
    self.rules[#self.rules + 1] = { a = a, b = b, fn = fn }
end

function Game:find(tag)
    local out = {}
    for _, s in ipairs(self.sprites) do
        if not s.dead and (tag == nil or s.tag == tag) then out[#out + 1] = s end
    end
    return out
end

-- ワールド座標(wx, wy)にいるスプライト(一番手前)
function Game:sprite_at(wx, wy, tag)
    local list = self.sprites
    for i = #list, 1, -1 do
        local s = list[i]
        if not s.dead and s.visible and (tag == nil or s.tag == tag) then
            local bx, by, bw, bh = box(s)
            if wx >= bx and wx < bx + bw and wy >= by and wy < by + bh then return s end
        end
    end
end

function Game:_check_rules()
    local list = self.sprites
    for _, r in ipairs(self.rules) do
        for i = 1, #list do
            local a = list[i]
            if not a.dead and a.tag == r.a then
                for j = 1, #list do
                    local b = list[j]
                    if b ~= a and not b.dead and b.tag == r.b and M.overlaps(a, b) then
                        r.fn(a, b)
                        if a.dead then break end
                    end
                end
            end
        end
    end
end

function Game:_compact()
    local list, n = self.sprites, 0
    for i = 1, #list do
        local s = list[i]
        if s.dead then
            if s._rv then self:_push_rect(s._rx, s._ry, s._rw, s._rh) end
            s._rv = nil
        else
            n = n + 1
            list[n] = s
        end
    end
    for i = #list, n + 1, -1 do list[i] = nil end
    if self._order then
        table.sort(list, function(a, b)
            if a.layer ~= b.layer then return a.layer < b.layer end
            return a._seq < b._seq
        end)
        self._order = false
        self._full = true
    end
end

-- ---- タイルマップ ----

-- o: image, tile(=tile_w=tile_h。既定8) / tile_w, tile_h, x, y,
--    データは次のどれか:
--      rows={"#####", "#..P#"} + legend={["#"]=1, ...}(書いていない文字は0=空)
--      data="\1\1\0..." (1バイト=1マス) + cols
--      data={1,1,0,...} + cols
--    spawn={P=function(x, y) ... end}: rowsのその文字の位置(ワールド座標)で呼ぶ(マスは空にする)
--    solid=true(0以外は全部壁) / {1,2,...} / function(id) return bool end
function Game:tilemap(o)
    local m = setmetatable({ game = self }, Tilemap)
    m.image = self:_img(o.image)
    m.tw = o.tile_w or o.tile or 8
    m.th = o.tile_h or o.tile or m.tw
    m.x, m.y = o.x or 0, o.y or 0
    local spawns = {}
    if o.rows then
        local legend, sp, cols = o.legend or {}, o.spawn or {}, 0
        for _, row in ipairs(o.rows) do cols = max(cols, #row) end
        local parts = {}
        for r, row in ipairs(o.rows) do
            local bytes = {}
            for c = 1, cols do
                local ch = row:sub(c, c)
                if sp[ch] then
                    spawns[#spawns + 1] = { sp[ch], m.x + (c - 1) * m.tw, m.y + (r - 1) * m.th }
                    bytes[c] = 0
                else
                    bytes[c] = legend[ch] or 0
                end
            end
            parts[r] = string.char(table.unpack(bytes))
        end
        m.cols, m.data = cols, table.concat(parts)
    elseif type(o.data) == "string" then
        m.cols, m.data = o.cols, o.data
    elseif type(o.data) == "table" then
        local parts, d = {}, o.data
        for i = 1, #d, 200 do parts[#parts + 1] = string.char(table.unpack(d, i, min(i + 199, #d))) end
        m.cols, m.data = o.cols, table.concat(parts)
    else
        error("pico.game: tilemapにrowsかdataが要ります", 2)
    end
    if not m.cols or m.cols < 1 then error("pico.game: tilemapの列数(cols)がありません", 2) end
    m.rows = (#m.data + m.cols - 1) // m.cols
    local sol = o.solid
    if sol == true then m._solid = function(v) return v ~= 0 end
    elseif type(sol) == "table" then
        local set = {}
        for _, v in ipairs(sol) do set[v] = true end
        m._solid = function(v) return set[v] == true end
    elseif type(sol) == "function" then m._solid = sol end
    self.maps[#self.maps + 1] = m
    self._full = true
    for _, sp in ipairs(spawns) do sp[1](sp[2], sp[3]) end
    return m
end

function Tilemap:get(c, r)
    if c < 0 or r < 0 or c >= self.cols or r >= self.rows then return 0 end
    return self.data:byte(r * self.cols + c + 1) or 0
end

function Tilemap:set(c, r, v)
    if c < 0 or r < 0 or c >= self.cols or r >= self.rows then return end
    local i = r * self.cols + c + 1
    if self.data:byte(i) == v then return end
    self.data = self.data:sub(1, i - 1) .. string.char(v) .. self.data:sub(i + 1)
    local g = self.game
    g:_push_rect(g.x + round(self.x + c * self.tw - g.cam_x), g.y + round(self.y + r * self.th - g.cam_y), self.tw, self.th)
end

-- ワールド座標 → マス目(c, r)
function Tilemap:cell(wx, wy) return floor((wx - self.x) / self.tw), floor((wy - self.y) / self.th) end
-- ワールド座標のマスの値と、そのマス目
function Tilemap:at(wx, wy)
    local c, r = self:cell(wx, wy)
    return self:get(c, r), c, r
end
function Tilemap:is_solid(c, r) return self._solid ~= nil and self._solid(self:get(c, r)) end
-- 値がvのマス目を全部 { {c, r}, ... }
function Tilemap:find(v)
    local out, cols = {}, self.cols
    local i = 0
    while true do
        i = self.data:find(string.char(v), i + 1, true)
        if not i then break end
        out[#out + 1] = { (i - 1) % cols, (i - 1) // cols }
    end
    return out
end

function Tilemap:_row_solid(a, b0, b1, a_is_col)
    for b = b0, b1 do
        local v = a_is_col and self:get(a, b) or self:get(b, a)
        if self._solid(v) then return true end
    end
    return false
end

-- ---- カメラ ----

function Game:world_size()
    if self.world_w then return self.world_w, self.world_h end
    local w, h = self.vw, self.vh
    for _, m in ipairs(self.maps) do
        w = max(w, m.x + m.cols * m.tw)
        h = max(h, m.y + m.rows * m.th)
    end
    return w, h
end

function Game:set_world(w, h) self.world_w, self.world_h = w, h end

function Game:camera(x, y)
    local ww, wh = self:world_size()
    self.cam_x = max(0, min(x, ww - self.vw))
    self.cam_y = max(0, min(y, wh - self.vh))
end

-- スプライトを画面の中央に保つ(nilでやめる)
function Game:follow(s) self._target = s end

function Game:_follow()
    local s = self._target
    if not s then return end
    if s.dead then self._target = nil return end
    local cx, cy = s:center()
    self:camera(cx - self.vw / 2, cy - self.vh / 2)
end

-- ワールド座標 → 画面座標(pico.draw_*に渡せる)
function Game:to_screen(wx, wy)
    return self.x + round(wx) - round(self.cam_x), self.y + round(wy) - round(self.cam_y)
end

-- ---- 描画 ----

-- キャンバスの中の矩形(lx,ly,w,h)を描き直す(on_drawで描くHUDの中身を変えたとき等)
function Game:dirty(lx, ly, w, h)
    self:_push_rect(self.x + floor(lx), self.y + floor(ly), floor(w + 0.999), floor(h + 0.999))
end
function Game:redraw() self._full_canvas = true end

-- 次の_flushで描き直す矩形(画面座標)
function Game:_push_rect(x, y, w, h)
    local r = self._rects
    r[#r + 1] = { x, y, w, h }
end

local function sprite_rect(g, s, cx, cy)
    return g.x + round(s.x) - cx, g.y + round(s.y) - cy, s.w, s.h
end

function Game:_flush()
    local cx, cy = round(self.cam_x), round(self.cam_y)
    local full = self._full or cx ~= self._dcx or cy ~= self._dcy
    self._dcx, self._dcy = cx, cy
    local rects = self._rects
    for _, s in ipairs(self.sprites) do
        local x, y, w, h = sprite_rect(self, s, cx, cy)
        local v = s.visible
        if s._rv ~= v or s._rx ~= x or s._ry ~= y or s._rw ~= w or s._rh ~= h
            or s._rf ~= s.frame or s._rfx ~= s.flip_x or s._rfy ~= s.flip_y or s._rc ~= s.color then
            if not full then
                if s._rv then rects[#rects + 1] = { s._rx, s._ry, s._rw, s._rh } end
                if v then rects[#rects + 1] = { x, y, w, h } end
            end
            s._rv, s._rx, s._ry, s._rw, s._rh = v, x, y, w, h
            s._rf, s._rfx, s._rfy, s._rc = s.frame, s.flip_x, s.flip_y, s.color
        end
    end
    if self._full_canvas then
        pico.invalidate(self.canvas)
        self._full_canvas = false
    elseif full or #rects > 24 then
        -- カメラが動いた等: 描き直すのはワールドの見える範囲だけ(画面ボタンの帯は変わらない)。
        -- ボタンの帯に入る矩形(押した/離した)は別に積む
        pico.mark_dirty(self.x, self.y, self.vw, self.vh)
        local x0, y0, x1, y1 = self.x, self.y + self.vh, self.x + self.w, self.y + self.h
        for i = 1, #rects do
            local r = rects[i]
            local ax, ay = max(r[1], x0), max(r[2], y0)
            local bx, by = min(r[1] + r[3], x1), min(r[2] + r[4], y1)
            if bx > ax and by > ay then pico.mark_dirty(ax, ay, bx - ax, by - ay) end
        end
    else
        local x0, y0, x1, y1 = self.x, self.y, self.x + self.w, self.y + self.h
        for i = 1, #rects do
            local r = rects[i]
            local ax, ay = max(r[1], x0), max(r[2], y0)
            local bx, by = min(r[1] + r[3], x1), min(r[2] + r[4], y1)
            if bx > ax and by > ay then pico.mark_dirty(ax, ay, bx - ax, by - ay) end
        end
    end
    for i = #rects, 1, -1 do rects[i] = nil end
    self._full = false
end

function Game:_render()
    local ax, ay, aw, ah = pico.get_draw_area()
    if aw <= 0 or ah <= 0 then ax, ay, aw, ah = self.x, self.y, self.w, self.h end
    -- ワールドは画面ボタンの帯へはみ出さない
    local ix, iy = max(ax, self.x), max(ay, self.y)
    local jx, jy = min(ax + aw, self.x + self.vw), min(ay + ah, self.y + self.vh)
    local cx, cy = round(self.cam_x), round(self.cam_y)
    local ox, oy = self.x - cx, self.y - cy
    if jx > ix and jy > iy then
        pico.set_draw_area(ix, iy, jx - ix, jy - iy)
        for _, m in ipairs(self.maps) do
            if m.image and m.visible ~= false then
                pico.draw_tilemap(m.image, m.tw, m.th, m.data, m.cols, ox + round(m.x), oy + round(m.y))
            end
        end
        if self.on_draw_world then self:on_draw_world(ox, oy) end
        for _, s in ipairs(self.sprites) do
            if s.visible and not s.dead then
                local x, y, w, h = sprite_rect(self, s, cx, cy)
                if x < jx and x + w > ix and y < jy and y + h > iy then
                    if s.draw then
                        s:draw(x, y)
                    elseif s.image then
                        local iw = self._iw[s.image]
                        if not iw then iw = pico.image_size(s.image); self._iw[s.image] = iw end
                        local per = max(1, iw // w)
                        local f = s.frame or 0
                        pico.draw_image_part(s.image, x, y, (f % per) * w, (f // per) * h, w, h,
                                             s.flip_x, s.flip_y)
                    elseif s.color then
                        pico.fill_rect(x, y, w, h, s.color)
                    end
                end
            end
        end
        local st = self.cur_state
        if st and st.draw then st.draw(self, self.x, self.y) end
        if self.on_draw then self:on_draw(self.x, self.y) end
        pico.set_draw_area(ax, ay, aw, ah)
    end
    for _, b in ipairs(self.buttons) do
        if b.visible ~= false then self:_draw_button(b) end
    end
end

local ARROW = {
    left  = { -1, 0 }, right = { 1, 0 }, up = { 0, -1 }, down = { 0, 1 },
}

function Game:_draw_button(b)
    local x, y, w, h = self.x + b.x, self.y + b.y, b.w, b.h
    local on = self._held[b.name]
    pico.fill_rect(x + 1, y + 1, w - 2, h - 2, on and 7 or 8)   -- 押している間は明るい灰
    pico.draw_rect(x + 1, y + 1, w - 2, h - 2, 0)
    local fg = on and 0 or 15
    local mx, my = x + w // 2, y + h // 2
    local a = ARROW[b.label]
    if a then
        local s = min(w, h) // 4
        local dx, dy = a[1], a[2]
        -- 先端と、根元の2点
        pico.fill_triangle(mx + dx * s, my + dy * s,
                           mx - dx * s - dy * s, my - dy * s - dx * s,
                           mx - dx * s + dy * s, my - dy * s + dx * s, fg)
    else
        local t = string.upper(tostring(b.label))
        local tw = pico.text_width(t, 0)
        pico.draw_text(mx - tw // 2, my - 8, t, fg, 0)   -- 16pxの文字
    end
end

return M
)LUA";

    inline const char* Source(const char* name) {
        if (!name) return nullptr;
        if (strcmp(name, "pico.ui") == 0) return kUi;
        if (strcmp(name, "pico.async") == 0) return kAsync;
        if (strcmp(name, "pico.tween") == 0) return kTween;
        if (strcmp(name, "pico.game") == 0) return kGame;
        return nullptr;
    }
}
