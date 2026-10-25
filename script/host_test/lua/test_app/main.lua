-- pico_test_test.lua(テストランナー自身のテスト)が読む小さなアプリ。
local util = require("util")
local game = require("pico.tween")
count = 0
local label = pico.create("Label")
pico.set_name(label, "count")
pico.set(label, "text", "0")
local btn = pico.create("Button")
pico.set(btn, "text", "+1")
pico.on(btn, "press_end", function() count = util.inc(count) pico.set(label, "text", tostring(count)) end)
pico.every(100, function() count = count + 10 end)
local data = pico.sd_read(pico.path_join(pico.app_dir(), "data.txt"))
pico.set(label, "data", data)
SANDBOX = { io = io, dofile = dofile, package = package, debug = debug, exit = os.exit, time = os.time,
            dump = string.dump, tween = game }
function setup() pico.set(label, "setup", true) end
function loop(dt) pico.set(label, "dt", dt) end
pico.on_key(function(k) if k == "enter" then count = 100 return true end end)
