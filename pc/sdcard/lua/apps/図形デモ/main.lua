-- 図形ウィジェット(Rect/Ellipse/Line/Triangle)のデモ。
-- SDスキャン式Luaアプリ(LuaAppScanner)として"/lua/apps/図形デモ/"配下に置くだけで
-- ランチャへ現れる(App_List.cppには一切手を加えない)。
--
-- pico.create("Canvas")のrenderコールバックで毎フレーム描き直す直接描画とは違い、
-- ここで作る図形ウィジェットは他のウィジェットと同じく「置いたら値が変わるまで
-- 自動的に維持される」普通の部品であることを示すのが狙い。

local x, y, w, h = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "text", "図形ウィジェット")

-- Rect: 塗りつぶし
local rect_filled = pico.create("Rect")
pico.set(rect_filled, "x", x + margin)
pico.set(rect_filled, "y", y + margin + 24)
pico.set(rect_filled, "w", 50)
pico.set(rect_filled, "h", 34)
pico.set(rect_filled, "color", 9) -- PICO_BLUE

-- Rect: 枠線のみ(thickness=3)
local rect_outline = pico.create("Rect")
pico.set(rect_outline, "x", x + margin + 60)
pico.set(rect_outline, "y", y + margin + 24)
pico.set(rect_outline, "w", 50)
pico.set(rect_outline, "h", 34)
pico.set(rect_outline, "color", 12) -- PICO_RED
pico.set(rect_outline, "filled", false)
pico.set(rect_outline, "thickness", 3)

-- Ellipse: 正円(塗りつぶし)
local circle = pico.create("Ellipse")
pico.set(circle, "x", x + margin + 120)
pico.set(circle, "y", y + margin + 24)
pico.set(circle, "w", 34)
pico.set(circle, "h", 34)
pico.set(circle, "color", 10) -- PICO_GREEN

-- Ellipse: 横長の輪郭のみ
local ellipse_outline = pico.create("Ellipse")
pico.set(ellipse_outline, "x", x + margin + 164)
pico.set(ellipse_outline, "y", y + margin + 24)
pico.set(ellipse_outline, "w", 46)
pico.set(ellipse_outline, "h", 30)
pico.set(ellipse_outline, "color", 5) -- PICO_PURPLE
pico.set(ellipse_outline, "filled", false)

-- Line: 太さ3の斜線
local line = pico.create("Line")
pico.set(line, "x1", x + margin)
pico.set(line, "y1", y + margin + 70)
pico.set(line, "x2", x + margin + 60)
pico.set(line, "y2", y + margin + 100)
pico.set(line, "color", 0) -- PICO_BLACK
pico.set(line, "thickness", 3)

-- Triangle: 塗りつぶし
local triangle_filled = pico.create("Triangle")
pico.set(triangle_filled, "x1", x + margin + 80)
pico.set(triangle_filled, "y1", y + margin + 100)
pico.set(triangle_filled, "x2", x + margin + 100)
pico.set(triangle_filled, "y2", y + margin + 70)
pico.set(triangle_filled, "x3", x + margin + 120)
pico.set(triangle_filled, "y3", y + margin + 100)
pico.set(triangle_filled, "color", 14) -- PICO_YELLOW

-- Triangle: 輪郭のみ
local triangle_outline = pico.create("Triangle")
pico.set(triangle_outline, "x1", x + margin + 140)
pico.set(triangle_outline, "y1", y + margin + 100)
pico.set(triangle_outline, "x2", x + margin + 160)
pico.set(triangle_outline, "y2", y + margin + 70)
pico.set(triangle_outline, "x3", x + margin + 180)
pico.set(triangle_outline, "y3", y + margin + 100)
pico.set(triangle_outline, "color", 8) -- PICO_DARKGREY
pico.set(triangle_outline, "filled", false)
pico.set(triangle_outline, "thickness", 2)

-- x/yは形全体の平行移動であることを見せる: ボタンでLineとTriangleをまとめて動かす
local moved = false
local move_button = pico.create("Button")
pico.set(move_button, "x", x + margin)
pico.set(move_button, "y", y + margin + 130)
pico.set(move_button, "w", 140)
pico.set(move_button, "h", 26)
pico.set(move_button, "text", "x/yで平行移動")
pico.on(move_button, "press_start", function()
    local dy = moved and -20 or 20
    pico.set(line, "y", pico.get(line, "y") + dy)
    pico.set(triangle_filled, "y", pico.get(triangle_filled, "y") + dy)
    moved = not moved
end)

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 166)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 26)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    pico.pop()
end)
