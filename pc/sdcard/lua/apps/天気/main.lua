-- 天気: 天気予報 API(livedoor 天気互換。https://weather.tsukumijima.net/)で、
-- 今日・明日・明後日の天気/最高最低気温/降水確率を見る。気象庁のデータ。
-- 都市は「都市」ボタンで選ぶ(最後に選んだ都市は store.json に覚える)。
-- 非公式のAPIなので、構造が変わると取れなくなることがある。
-- ※LuaSceneが読む main.lua は16KiBまで。
local X, Y, W, H = pico.content_rect()

local API = "https://weather.tsukumijima.net/api/forecast/city/"

-- livedoor 天気の都市ID(各都道府県の代表地点)
local CITIES = {
    { "札幌", "016010" }, { "青森", "020010" }, { "盛岡", "030010" }, { "仙台", "040010" },
    { "秋田", "050010" }, { "山形", "060010" }, { "福島", "070010" }, { "水戸", "080010" },
    { "宇都宮", "090010" }, { "前橋", "100010" }, { "さいたま", "110010" }, { "千葉", "120010" },
    { "東京", "130010" }, { "横浜", "140010" }, { "新潟", "150010" }, { "富山", "160010" },
    { "金沢", "170010" }, { "福井", "180010" }, { "甲府", "190010" }, { "長野", "200010" },
    { "岐阜", "210010" }, { "静岡", "220010" }, { "名古屋", "230010" }, { "津", "240010" },
    { "大津", "250010" }, { "京都", "260010" }, { "大阪", "270000" }, { "神戸", "280010" },
    { "奈良", "290010" }, { "和歌山", "300010" }, { "鳥取", "310010" }, { "松江", "320010" },
    { "岡山", "330010" }, { "広島", "340010" }, { "山口", "350010" }, { "徳島", "360010" },
    { "高松", "370000" }, { "松山", "380010" }, { "高知", "390010" }, { "福岡", "400010" },
    { "佐賀", "410010" }, { "長崎", "420010" }, { "熊本", "430010" }, { "大分", "440010" },
    { "宮崎", "450010" }, { "鹿児島", "460010" }, { "那覇", "471010" },
}

local store = pico.store_load() or {}
local city = 13
if type(store.city) == "string" then
    for i, c in ipairs(CITIES) do if c[2] == store.city then city = i end end
end

local data = nil          -- 取れた予報
local status = ""         -- 取得中・失敗の理由
local req = nil           -- 通信中のリクエストID
local show_text = false   -- 概要の表示
local canvas, tv, btn_text

-- 数字の文字列(null や "--" は nil)
local function num(v)
    if v == nil then return nil end
    local s = tostring(v)
    if s == "" or s == "--" or s == "--%" then return nil end
    return s
end

local function parse(body)
    local r = pico.json_decode(body)
    if type(r) ~= "table" or type(r.forecasts) ~= "table" then return nil end
    local loc = type(r.location) == "table" and r.location or {}
    local d = { city = loc.city or CITIES[city][1], pref = loc.prefecture or "",
                office = r.publishingOffice or "", days = {} }
    -- "2026/10/14 17:00:00" → "10/14 17:00"
    local pt = r.publicTimeFormatted or ""
    local mo, dd, hh, mi = pt:match("%d+[/%-](%d+)[/%-](%d+)%s+(%d+):(%d+)")
    d.time = mo and string.format("%d/%d %s:%s", tonumber(mo), tonumber(dd), hh, mi) or ""
    for i, f in ipairs(r.forecasts) do
        local t = type(f.temperature) == "table" and f.temperature or {}
        local tmax = type(t.max) == "table" and t.max.celsius or nil
        local tmin = type(t.min) == "table" and t.min.celsius or nil
        local cr = type(f.chanceOfRain) == "table" and f.chanceOfRain or {}
        local md = tostring(f.date or ""):match("%d+%-(%d+%-%d+)") or ""
        md = md:gsub("^0", ""):gsub("%-0?", "/")
        d.days[i] = {
            label = f.dateLabel or "", date = md, telop = f.telop or "",
            max = num(tmax), min = num(tmin),
            rain = { num(cr.T00_06), num(cr.T06_12), num(cr.T12_18), num(cr.T18_24) },
        }
    end
    local desc = type(r.description) == "table" and r.description or {}
    local head = (type(r.title) == "string" and r.title or "") .. "\n"
    d.text = head .. (desc.headlineText and desc.headlineText ~= "" and (desc.headlineText .. "\n\n") or "")
        .. (desc.bodyText or desc.text or "")
    return d
end

-- ---- 描画 ----
local SUN, GREY, BLUE, RED, LGREY, DGREY = 14, 8, 9, 12, 7, 8

-- 天気の絵(telop の最初の文字で決める)。x,y は左上、36x36
local function weather_icon(telop, x, y)
    local c = telop:sub(1, 3)
    local cx, cy = x + 18, y + 18
    local function cloud(ox, oy, col)
        pico.fill_circle(ox + 10, oy + 18, 8, col)
        pico.fill_circle(ox + 20, oy + 14, 10, col)
        pico.fill_circle(ox + 28, oy + 19, 7, col)
        pico.fill_rect(ox + 10, oy + 18, 18, 8, col)
    end
    if c == "晴" then
        pico.fill_circle(cx, cy, 9, SUN)
        for k = 0, 7 do
            local a = k * math.pi / 4
            pico.draw_line(cx + math.floor(math.cos(a) * 12), cy + math.floor(math.sin(a) * 12),
                           cx + math.floor(math.cos(a) * 16), cy + math.floor(math.sin(a) * 16), SUN)
        end
    elseif c == "雨" then
        cloud(x, y - 4, GREY)
        for k = 0, 2 do pico.draw_line(x + 10 + k * 8, y + 25, x + 7 + k * 8, y + 33, BLUE, 2) end
    elseif c == "雪" then
        cloud(x, y - 4, LGREY)
        for k = 0, 2 do
            pico.fill_circle(x + 10 + k * 8, y + 29, 2, BLUE)
        end
    else  -- 曇・その他
        cloud(x, y, GREY)
    end
end

local CARD_H = 64
local function draw_day(d, y)
    pico.draw_line(X + 4, y, X + W - 5, y, LGREY)
    weather_icon(d.telop, X + 6, y + 14)
    local tx = X + 48
    pico.draw_text(tx, y + 3, d.label .. " " .. d.date, DGREY, 0)
    -- 気温は右寄せ。「高20 低12」を色分けして描く
    local right = X + W - 6
    local lo = d.min and ("低" .. d.min) or "低--"
    local hi = d.max and ("高" .. d.max) or "高--"
    local wlo = pico.text_width(lo, 0)
    pico.draw_text(right - wlo, y + 3, lo, BLUE, 0)
    pico.draw_text(right - wlo - 8 - pico.text_width(hi, 0), y + 3, hi, RED, 0)
    -- 天気(長いときは小さい字)
    local fs = pico.text_width(d.telop, 1) <= (X + W - 6 - tx) and 1 or 0
    pico.draw_text(tx, y + 20, d.telop, 0, fs)
    -- 降水確率(0-6, 6-12, 12-18, 18-24時)
    local parts = {}
    for i = 1, 4 do parts[i] = d.rain[i] and d.rain[i]:gsub("%%", "") or "-" end
    pico.draw_text(tx, y + 46, "降水 " .. table.concat(parts, "/") .. "%", DGREY, 0)
end

local function render()
    local top = Y + 34
    if not data then
        pico.draw_text(X + 8, top + 8, CITIES[city][1], 0, 1)
        pico.draw_text_wrapped(X + 8, top + 44, W - 16, status, status:find("失敗") and RED or DGREY, 0)
        return
    end
    pico.draw_text(X + 6, top + 2, data.city, 0, 1)
    local sub = (data.pref ~= "" and (data.pref .. " ") or "") .. (data.time ~= "" and ("発表 " .. data.time) or "")
    pico.draw_text(X + 6, top + 28, sub, DGREY, 0)
    for i, d in ipairs(data.days) do
        if i > 3 then break end
        draw_day(d, top + 48 + (i - 1) * CARD_H)
    end
    if status ~= "" then
        pico.draw_text(X + 6, Y + H - 18, status, DGREY, 0)
    end
end

local function redraw() pico.invalidate(canvas) end

-- ---- 通信 ----
local function load_weather()
    if req then pico.http_cancel(req) end
    status = "取得中…"
    data = nil
    redraw()
    local my = city
    req = pico.http_request("GET", API .. CITIES[city][2], nil, nil,
        function(ok, code, body, err)
            req = nil
            if my ~= city then return end
            if not ok then
                status = "取得に失敗しました: " .. tostring(err)
            elseif code ~= 200 or not body then
                status = "取得に失敗しました(HTTP " .. tostring(code) .. ")"
            else
                local d = parse(body)
                if d then
                    data, status = d, ""
                    pico.text_set(tv, d.text)
                else
                    status = "取得に失敗しました: 予報を読み取れません"
                end
            end
            redraw()
        end, { keep_alive = true, headers = { Accept = "application/json" } })
    if not req then
        status = "取得に失敗しました: 通信できません(ネットワーク権限/混雑)"
        redraw()
    end
end

local function select_city()
    local names = {}
    for i, c in ipairs(CITIES) do names[i] = c[1] end
    local dlg = pico.show_choice("都市を選ぶ", names)
    pico.on(dlg, "closed", function(_, ok, idx)
        if ok and CITIES[idx + 1] then
            city = idx + 1
            store.city = CITIES[city][2]
            pico.store_save(store)
            load_weather()
        end
    end)
end

local function set_text_mode(on)
    show_text = on
    pico.set(tv, "visible", on)
    pico.set(canvas, "visible", not on)
    pico.set(btn_text, "text", on and "予報" or "概要")
    if not on then redraw() end
end

local function back()
    if req then pico.http_cancel(req) end
    pico.http_close()
    pico.pop()
end

-- ---- 部品 ----
local function button(text, x, w, fn)
    local b = pico.create("Button")
    pico.set(b, "text", text)
    pico.set(b, "x", x)
    pico.set(b, "y", Y)
    pico.set(b, "w", w)
    pico.set(b, "h", 30)
    pico.on(b, "press_end", fn)
    return b
end

local bw = (W - 6) // 4
button("戻る", X, bw, back)
button("都市", X + bw + 2, bw, select_city)
button("更新", X + (bw + 2) * 2, bw, load_weather)
btn_text = button("概要", X + (bw + 2) * 3, bw, function() set_text_mode(not show_text) end)

canvas = pico.create("Canvas")
pico.set(canvas, "x", X)
pico.set(canvas, "y", Y + 32)
pico.set(canvas, "w", W)
pico.set(canvas, "h", H - 32)
pico.on(canvas, "render", render)

tv = pico.create("TextView")
pico.set(tv, "x", X + 2)
pico.set(tv, "y", Y + 34)
pico.set(tv, "w", W - 4)
pico.set(tv, "h", H - 36)
pico.set(tv, "visible", false)

pico.on_back(back)
load_weather()
