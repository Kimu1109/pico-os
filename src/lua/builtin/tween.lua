-- ---- pico.tween: 値を一定時間かけて変えていく ----
--   local tween = require("pico.tween")
--   local h = tween.start{ from = 0, to = 100, duration = 500, ease = "ease_out",
--       on_update = function(v) pico.set(bar, "value", v) end, on_done = function() end }
--   tween.cancel(h)

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
