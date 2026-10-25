-- QRコード: 入れた文字(URLなど)をQRコードにして画面に出す。スマホのカメラで読み取れる。
--
-- QRの部品は pico.create("QRCode")。文字は pico.text_set で渡す(pico.set の "text" は
-- 254バイトで黙って切れるので、長さの分からない文字列は text_set で入ったかを見る)。
-- 誤り訂正の強さ(L/M/Q/H)は上のタブで選ぶ。最後の文字と強さは store.json に覚える。
local X, Y, W, H = pico.content_rect()
local GAP = 4
local ROW_H = 30

local store = pico.store_load() or {}
local text = type(store.text) == "string" and store.text or "https://example.com/"
local ecc = math.tointeger(store.ecc) or pico.QR_ECC_MEDIUM

local qr, info

local function refresh()
    if not pico.text_set(qr, text) then
        pico.set(info, "text", "長すぎてQRに入りません")
        return
    end
    local n = pico.get(qr, "qr_size")
    if n == 0 then
        pico.set(info, "text", "文字を入れるとQRになります")
    else
        pico.set(info, "text", string.format("%dバイト / %d×%d升", #text, n, n))
    end
end

local function save()
    pico.store_save({ text = text, ecc = ecc })
end

-- ---- 1段目: 戻る + 誤り訂正の強さ ----
local back = pico.create("Button")
pico.set(back, "text", "戻る")
pico.set(back, "x", X)
pico.set(back, "y", Y)
pico.set(back, "w", 60)
pico.set(back, "h", ROW_H)
pico.on(back, "press_end", function() pico.pop() end)

local tabs = pico.create("TabBar")
pico.set(tabs, "x", X + 60 + GAP)
pico.set(tabs, "y", Y)
pico.set(tabs, "w", W - 60 - GAP)
pico.set(tabs, "h", ROW_H - 4)
for _, name in ipairs({ "L", "M", "Q", "H" }) do pico.tab_add(tabs, name) end
pico.set(tabs, "tab_selected", ecc)
pico.on(tabs, "tab_changed", function(id)
    ecc = pico.get(id, "tab_selected")
    pico.set(qr, "ecc", ecc)
    refresh()
    save()
end)

-- ---- 2段目: 文字 ----
local box_y = Y + ROW_H + GAP
local box = pico.create("Textbox")
pico.set(box, "x", X + 2)
pico.set(box, "y", box_y)
pico.set(box, "is_single_line", false)
pico.set(box, "max_width", W - 4)
pico.set(box, "max_height", 48)
pico.set(box, "font_size", pico.FONT_SMALL)
pico.set(box, "placeholder", "QRにする文字(URLなど)")
pico.set(box, "text", text)
pico.on(box, "text_input", function(id)
    text = pico.get(id, "text")
    refresh()
end)
pico.on(box, "text_changed", function() save() end)

-- ---- 残り: QR(正方形で中央)と、その下に大きさ ----
local info_h = 20
local qr_top = box_y + 48 + GAP
local side = math.min(W, H - (qr_top - Y) - info_h)
qr = pico.create("QRCode")
pico.set(qr, "x", X + (W - side) // 2)
pico.set(qr, "y", qr_top)
pico.set(qr, "w", side)
pico.set(qr, "h", side)
pico.set(qr, "ecc", ecc)

info = pico.create("Label")
pico.set(info, "x", X + 4)
pico.set(info, "y", qr_top + side)
pico.set(info, "font_size", pico.FONT_SMALL)
pico.set(info, "text", "")

refresh()
