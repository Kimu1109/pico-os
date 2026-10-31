-- pico_test: Luaアプリ作者向けのテストランナー(`picoos lua-test`)。
-- アプリ(main.lua とそのフォルダ)を pico_mock.lua(pico.* のLua製の偽物)の上で読み込み、
-- テストファイル(*_test.lua)に書いた test(...) を順に動かして結果を出す。実機もPCビルドも要らない。
--
-- テストファイルの書き方(詳しくは lua-api-doc の guide/app/testing.md):
--
--   -- /lua/apps/わたしのゲーム/tests/menu_test.lua
--   test("はじめは得点が0", function()
--       local app = load_app()               -- テストファイルの上のフォルダの main.lua を読み、setup() まで呼ぶ
--       eq(app:widget("score").text, "0")    -- pico.set_name で付けた名前か、text で引く
--       app:tap("スタート")                   -- press_start → press_end を鳴らす
--       app:frame(100)                       -- タイマーを100ms進めて loop(100) を呼ぶ
--       ok(app.mock.draws > 0, "描いた")
--   end)
--
-- アプリは実機のサンドボックスに近い環境(io / dofile / loadfile / package / debug / os.exit 等が無い、
-- __gc 不可、require はアプリのフォルダと pico.* の同梱モジュールだけ)で動く。偽物に無い pico.* を呼ぶと
-- 「pico_mock に無い関数」のエラーになるので、load_app{ stubs = { 名前 = 関数 } } で足す。
--
-- 呼び出し: cli-tool/picoos lua-test が script/lua_test/pico_lua_test(vendorしたLuaで動く下請け)を作って
-- main(arg) を呼ぶ。ホストテスト(script/host_test/pico_test_test.lua)からは dofile して M.run_files を使う。
local M = {}
local mock = dofile(ROOT_DIR .. "/script/host_test/lua/pico_mock.lua")
M.mock = mock
M.verbose = false

-- ---------------- アプリの環境(サンドボックスの真似) ----------------

-- アプリから見える標準の関数・ライブラリ(lua-api-doc guide/app/debugging.md「サンドボックス」)。
-- ライブラリは浅く写してから渡す(アプリが math 等を書き換えてもテストや次のアプリへ漏れない)
local BASE_FUNCS = { "assert", "error", "getmetatable", "ipairs", "next", "pairs", "pcall", "rawequal", "rawget",
                     "rawlen", "rawset", "select", "tonumber", "tostring", "type", "xpcall", "collectgarbage" }
local BASE_LIBS = { "coroutine", "math", "string", "table", "utf8" }
local OS_ALLOWED = { "time", "clock", "date", "difftime" }

local function copy(t)
    local o = {}
    for k, v in pairs(t) do o[k] = v end
    return o
end

-- 本物の require と同じ名前の決まり("a.b"。英数字・_・-、47文字まで)
local function module_name_valid(name)
    if type(name) ~= "string" or name == "" or #name > 47 then return false end
    if name:sub(1, 1) == "." or name:sub(-1) == "." or name:find("..", 1, true) then return false end
    return name:find("^[%w_%-%.]+$") ~= nil
end

local function read_file(path)
    local f = io.open(path, "rb")
    if not f then return nil end
    local s = f:read("a")
    f:close()
    return s
end

local function file_exists(path) return read_file(path) ~= nil end

local function make_env(app)
    local env = {}
    for _, k in ipairs(BASE_FUNCS) do env[k] = _G[k] end
    for _, k in ipairs(BASE_LIBS) do env[k] = copy(_G[k]) end
    env.string.dump = nil
    env.os = {}
    for _, k in ipairs(OS_ALLOWED) do env.os[k] = os[k] end
    env._VERSION = _VERSION
    env._G = env
    env.pico = app.pico

    function env.setmetatable(t, mt)
        if type(mt) == "table" and rawget(mt, "__gc") ~= nil then
            error("setmetatable: __gc を持つメタテーブルは使えません", 2)
        end
        return setmetatable(t, mt)
    end
    -- テキストだけ。環境を渡さなければアプリの環境(本物も同じ)
    function env.load(chunk, name, mode, e)
        if mode ~= nil and mode ~= "t" then error("load: バイトコードは読めません", 2) end
        return load(chunk, name, "t", e or env)
    end
    function env.print(...)
        local t = {}
        for i = 1, select("#", ...) do t[i] = tostring((select(i, ...))) end
        local s = table.concat(t, "\t")
        app.mock.logs[#app.mock.logs + 1] = s
        if M.verbose then io.write("       | ", s, "\n") end
    end

    local loading = {}
    function env.require(name)
        if not module_name_valid(name) then error("require: モジュールの名前が不正です: " .. tostring(name), 2) end
        local cached = app.loaded[name]
        if cached ~= nil then return cached end
        if loading[name] then error("require: モジュールが循環しています: " .. name, 2) end
        local candidates
        if name:sub(1, 5) == "pico." then
            -- OSに同梱のモジュール(src/lua/builtin/)。アプリのフォルダより優先し、"pico." はここだけ
            candidates = { ROOT_DIR .. "/src/lua/builtin/" .. name:sub(6) .. ".lua" }
        else
            local rel = name:gsub("%.", "/")
            candidates = { app.dir .. "/" .. rel .. ".lua", app.dir .. "/" .. rel .. "/init.lua" }
        end
        for _, path in ipairs(candidates) do
            local src = read_file(path)
            if src then
                local fn, err = load(src, "@" .. path, "t", env)
                if not fn then error(err, 0) end
                loading[name] = true
                local ok, v = pcall(fn, name)
                loading[name] = nil
                if not ok then error(v, 0) end
                if v == nil then v = true end
                app.loaded[name] = v
                return v
            end
        end
        error("require: モジュールが見つかりません: " .. name .. " (" .. table.concat(candidates, " / ") .. ")", 2)
    end
    return env
end

-- ---------------- アプリを読み込む ----------------

local App = {}
App.__index = App

-- テストファイルのパスから、上へたどって main.lua のあるフォルダを探す
local function find_app_dir(path)
    local dir = path:match("^(.*)/[^/]*$") or "."
    while dir and dir ~= "" do
        if file_exists(dir .. "/main.lua") then return dir end
        dir = dir:match("^(.*)/[^/]*$")
    end
    return nil
end
M.find_app_dir = find_app_dir

local function run_chunk(app, path)
    local src = read_file(path)
    if not src then return false end
    local fn, err = load(src, "@" .. path, "t", app.env)
    if not fn then error(err, 0) end
    fn()
    return true
end

-- load_app{ dir=, args=, launch_reason={tag,data}, pad_connected=, stubs={}, setup=false, sd_dir=, lang= }
--   lang: pico.lang() の値(既定 "ja")。"en" ならアプリの lang/en.lua で pico.tr する
--   dir     … アプリのフォルダ(既定: テストファイルの上の main.lua のあるフォルダ)
--   sd_dir  … アプリのSD上のパス(pico.app_dir() の値。既定: pc/sdcard/ の下ならそこからの位置、
--              それ以外は /lua/apps/<フォルダ名>)。SDのそのパスの下を読むと dir の中のファイルが返る
--   setup   … false なら setup() を呼ばない(既定は本物と同じく main.lua の後に呼ぶ)
function M.load_app(opts)
    opts = opts or {}
    local dir = opts.dir or M.current_app_dir
    if not dir then error("load_app: アプリのフォルダが分かりません(dir = ... を渡してください)", 2) end
    dir = dir:gsub("/+$", "")
    if not file_exists(dir .. "/main.lua") then error("load_app: " .. dir .. "/main.lua がありません", 2) end

    local sd_dir = opts.sd_dir
    local sdcard = ROOT_DIR .. "/pc/sdcard"
    local sd_root
    if not sd_dir and dir:sub(1, #sdcard + 1) == sdcard .. "/" then
        -- リポジトリの pc/sdcard/ の下のアプリは、SD全体をそこへ当てる(他のアプリのファイルも読める)
        sd_dir = dir:sub(#sdcard + 1)
        sd_root = sdcard
    else
        sd_dir = sd_dir or ("/lua/apps/" .. (dir:match("([^/]+)$") or "app"))
        sd_dir = sd_dir:gsub("/+$", "")
        local prefix = sd_dir .. "/"
        sd_root = function(p)
            if p:sub(1, #prefix) == prefix then return dir .. "/" .. p:sub(#prefix + 1) end
        end
    end

    local pico = mock.install({ app_dir = sd_dir, args = opts.args, launch_reason = opts.launch_reason,
                                pad_connected = opts.pad_connected, sd_root = sd_root, lang = opts.lang })
    for k, v in pairs(opts.stubs or {}) do pico[k] = v end
    -- 偽物に無い関数は、nil を呼んだときの分かりにくいエラーの代わりに名前を出して止める
    setmetatable(pico, { __index = function(_, k)
        error("pico_mock に無い関数です: pico." .. tostring(k) ..
              "(load_app{ stubs = { " .. tostring(k) .. " = function(...) end } } で足せます)", 2)
    end })

    local app = setmetatable({ mock = mock, pico = pico, dir = dir, sd_dir = sd_dir, loaded = {} }, App)
    app.env = make_env(app)
    -- LuaScene と同じく、同じフォルダの lib.lua を main.lua より先に実行する
    run_chunk(app, dir .. "/lib.lua")
    run_chunk(app, dir .. "/" .. (opts.main or "main.lua"))
    if opts.setup ~= false then app:call("setup") end
    return app
end

-- アプリのグローバル関数を呼ぶ(無ければ何もしない)
function App:call(name, ...)
    local fn = rawget(self.env, name)
    if type(fn) == "function" then return fn(...) end
end

-- タイマーを ms 進め、loop(ms) を呼ぶ(既定16ms = 約60fps の1フレーム)
function App:frame(ms)
    ms = ms or 16
    self.mock.advance(ms)
    return self:call("loop", ms)
end

-- total_ms の間、step ms ごとに frame() を回す
function App:run(total_ms, step)
    step = step or 16
    local t = 0
    while t < total_ms do
        local d = math.min(step, total_ms - t)
        self:frame(d)
        t = t + d
    end
end

-- 条件を満たすウィジェットのIDの一覧(ID順)。{ type = "Button", text = "OK" } のようにプロパティで引く
function App:find_all(query)
    local ids = {}
    for id, w in pairs(self.mock.widgets) do
        local match = true
        if type(query) == "function" then
            match = query(w, id)
        else
            for k, v in pairs(query or {}) do
                if w[k] ~= v then match = false break end
            end
        end
        if match then ids[#ids + 1] = id end
    end
    table.sort(ids)
    return ids
end

-- IDか、pico.set_name の名前か、text の一致するウィジェット1つのID。見つからない・2つ以上ならエラー
function App:id(q)
    if type(q) == "number" then
        if not self.mock.widgets[q] then error("ウィジェットがありません: " .. q, 3) end
        return q
    end
    if type(q) == "string" then
        local named = self.mock.names[q]
        if named and self.mock.widgets[named] then return named end
        q = { text = q }
    end
    local ids = self:find_all(q)
    if #ids == 0 then error("ウィジェットが見つかりません: " .. M.show(q), 3) end
    if #ids > 1 then error("ウィジェットが" .. #ids .. "個あります: " .. M.show(q), 3) end
    return ids[1]
end

-- ウィジェットのプロパティの表(pico.set で書いた値。type / x / y / w / h / text / items / callbacks 等)
function App:widget(q) return self.mock.widgets[self:id(q)] end

function App:fire(q, event, ...) return self.mock.fire(self:id(q), event, ...) end

-- 押して離す(press_start → press_end)。x, y を省くとウィジェットの真ん中
function App:tap(q, x, y)
    local id = self:id(q)
    local w = self.mock.widgets[id]
    x = x or (w.x + w.w // 2)
    y = y or (w.y + w.h // 2)
    self.mock.fire(id, "press_start", x, y)
    if self.mock.widgets[id] then self.mock.fire(id, "press_end", x, y) end
end

-- 物理キーボードの打鍵(pico.on_key の関数を呼ぶ)。key は文字か "enter" 等の名前
function App:key(key, mods)
    local fn = self.mock.key_handler
    if not fn then return false end
    mods = mods or {}
    return fn(key, { ctrl = mods.ctrl or false, alt = mods.alt or false, shift = mods.shift or false }) and true or false
end

-- 戻る(on_back の関数があればそれ、無ければ pop)
function App:back() return self.pico.go_back() end

function App:close_dialog(id, is_ok, value) return self.mock.close_dialog(id, is_ok, value) end
function App:respond(index, ...) return self.mock.respond(index, ...) end
function App:last_dialog() return self.mock.dialogs[#self.mock.dialogs] end

-- ---------------- 確かめる関数 ----------------

-- 値を短い文字列に(表は中身まで、深さ3まで)
function M.show(v, depth)
    depth = depth or 0
    if type(v) == "string" then return string.format("%q", v) end
    if type(v) ~= "table" then return tostring(v) end
    if depth >= 3 then return "{...}" end
    local keys = {}
    for k in pairs(v) do keys[#keys + 1] = k end
    table.sort(keys, function(a, b)
        if type(a) == type(b) and (type(a) == "number" or type(a) == "string") then return a < b end
        return type(a) < type(b)
    end)
    local out = {}
    for i, k in ipairs(keys) do
        if i > 8 then out[#out + 1] = "..." break end
        local ks = (type(k) == "number" and k == i) and "" or ((type(k) == "string" and k:match("^[%a_][%w_]*$")) and k or ("[" .. M.show(k) .. "]")) .. "="
        out[#out + 1] = ks .. M.show(v[k], depth + 1)
    end
    return "{" .. table.concat(out, ", ") .. "}"
end

-- 違う所のパス("" なら同じ)
local function diff(a, b, path)
    if a == b then return nil end
    if type(a) ~= "table" or type(b) ~= "table" then return path end
    for k, v in pairs(a) do
        local d = diff(v, b[k], path .. "." .. tostring(k))
        if d then return d end
    end
    for k in pairs(b) do
        if a[k] == nil then return path .. "." .. tostring(k) end
    end
    return nil
end

local function fail(msg, level) error(msg, (level or 1) + 1) end

local function label(msg) return msg and (msg .. ": ") or "" end

-- 真なら通る
function M.ok(v, msg)
    if not v then fail(label(msg) .. "ok: 偽でした (" .. M.show(v) .. ")", 2) end
    return v
end

-- 同じなら通る(表は中身を比べる)
function M.eq(actual, expected, msg)
    local d = diff(actual, expected, "")
    if d then
        local where = d ~= "" and (" (" .. d:sub(2) .. " が違う)") or ""
        fail(label(msg) .. "eq: 期待 " .. M.show(expected) .. " / 実際 " .. M.show(actual) .. where, 2)
    end
end

function M.ne(actual, unexpected, msg)
    if not diff(actual, unexpected, "") then fail(label(msg) .. "ne: 同じ値でした " .. M.show(actual), 2) end
end

-- 小数の比較。実機と同じく小数は単精度なので、既定の許容差は1e-4
function M.near(actual, expected, eps, msg)
    eps = eps or 1e-4
    if type(actual) ~= "number" or math.abs(actual - expected) > eps then
        fail(label(msg) .. "near: 期待 " .. tostring(expected) .. " ± " .. tostring(eps) .. " / 実際 " .. tostring(actual), 2)
    end
end

-- fn がエラーを投げれば通る(pattern を渡すとメッセージの一致も見る。普通の文字列として探す)。メッセージを返す
function M.raises(fn, pattern, msg)
    local ok, err = pcall(fn)
    if ok then fail(label(msg) .. "raises: エラーになりませんでした", 2) end
    local s = tostring(err)
    if pattern and not s:find(pattern, 1, true) then
        fail(label(msg) .. "raises: エラーの文が違います: " .. s, 2)
    end
    return s
end

function M.fail(msg) fail(msg or "fail", 2) end

-- ---------------- 走らせる ----------------

local function traceback(e)
    if M.verbose then return debug.traceback(tostring(e), 2) end
    return tostring(e)
end

-- 1ファイル分。戻り値は 成功数, 失敗数
local function run_file(path, filter, out)
    local tests, before, after = {}, {}, {}
    local env = setmetatable({}, { __index = _G })
    env.test = function(name, fn) tests[#tests + 1] = { name = name, fn = fn } end
    env.before_each = function(fn) before[#before + 1] = fn end
    env.after_each = function(fn) after[#after + 1] = fn end
    for _, k in ipairs { "ok", "eq", "ne", "near", "raises", "fail", "load_app", "show" } do env[k] = M[k] end
    M.current_app_dir = find_app_dir(path)
    env.APP_DIR = M.current_app_dir

    out("== " .. path)
    local chunk, err = loadfile(path, "t", env)
    if chunk then
        local ok, e = xpcall(chunk, traceback)
        if not ok then chunk, err = nil, e end
    end
    if not chunk then
        out("[FAIL] (読み込み) " .. tostring(err))
        return 0, 1
    end

    local pass, failed, ran = 0, 0, 0
    for _, t in ipairs(tests) do
        if not filter or tostring(t.name):find(filter, 1, true) then
            ran = ran + 1
            local ok, e = xpcall(function()
                for _, f in ipairs(before) do f() end
                t.fn()
            end, traceback)
            -- 後片付けは失敗しても必ず呼ぶ(その失敗も数える)
            for _, f in ipairs(after) do
                local ok2, e2 = xpcall(f, traceback)
                if ok and not ok2 then ok, e = false, e2 end
            end
            if ok then
                pass = pass + 1
                out("[ OK ] " .. t.name)
            else
                failed = failed + 1
                out("[FAIL] " .. t.name)
                out("       " .. tostring(e):gsub("\n", "\n       "))
            end
        end
    end
    if ran == 0 then out("       (動かすテストがありません)") end
    M.current_app_dir = nil
    return pass, failed
end

-- files を順に動かす。opts = { filter = 名前の一部, out = 1行ずつ受ける関数 }。戻り値は 成功数, 失敗数
function M.run_files(files, opts)
    opts = opts or {}
    local out = opts.out or print
    local pass, failed = 0, 0
    for _, path in ipairs(files) do
        local p, f = run_file(path, opts.filter, out)
        pass, failed = pass + p, failed + f
    end
    out(string.format("-- %d件成功 / %d件失敗 (%dファイル)", pass, failed, #files))
    return pass, failed
end

-- pico_lua_test から呼ばれる入口。arg = { [-v] [-k 名前の一部] ファイル... }。戻り値が終了コード
function M.main(arg)
    local files, filter = {}, nil
    local i = 1
    while i <= #arg do
        local a = arg[i]
        if a == "-v" then M.verbose = true
        elseif a == "-k" then i = i + 1 filter = arg[i]
        else files[#files + 1] = a end
        i = i + 1
    end
    if #files == 0 then
        io.stderr:write("pico_test: テストファイルがありません\n")
        return 2
    end
    local _, failed = M.run_files(files, { filter = filter, out = function(s) io.write(s, "\n") end })
    io.stdout:flush()
    return failed == 0 and 0 or 1
end

return M
