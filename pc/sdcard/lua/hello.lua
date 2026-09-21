-- pico-os Luaアプリのサンプル。
-- LuaScene(src/gui/scenes/LuaScene.hpp)がSDから読み込んで実行する。
-- pico.create/set/get/on/add_child/popという最小限のAPIだけで
-- 「ボタンを押すと数字が増える画面 + ランチャへ戻るボタン」を組み立てる。
--
-- グローバル関数setup()/loop(dt)を定義すると、Arduino風に
-- setup()はonEnter()直後に1回、loop(dt)は毎フレーム(dtは前回からの経過ミリ秒)
-- 呼ばれる(どちらも定義は任意)。ここでは経過秒数を表示する例にした。

local x, y, w, h = pico.content_rect()
local margin = 10

local title = pico.create("Label")
pico.set(title, "x", x + margin)
pico.set(title, "y", y + margin)
pico.set(title, "font_size", 2) -- FontFn::Big(32px)
pico.set(title, "text", "Lua Hello")

local count = 0
local count_label = pico.create("Label")
pico.set(count_label, "x", x + margin)
pico.set(count_label, "y", y + margin + 40)
pico.set(count_label, "text", "count: 0")

local inc_button = pico.create("Button")
pico.set(inc_button, "x", x + margin)
pico.set(inc_button, "y", y + margin + 70)
pico.set(inc_button, "w", 80)
pico.set(inc_button, "h", 30)
pico.set(inc_button, "text", "+1")
pico.on(inc_button, "press_start", function()
    count = count + 1
    pico.set(count_label, "text", "count: " .. count)
end)

-- 画像のデモ: pico.image_load()は`.pimg`をこのLuaアプリ専用の固定長スロットへ
-- デコードして整数ハンドルを返す(ウィジェットではないのでpico.destroyの対象外)。
-- 描画は他のpico.draw_*と同じくCanvasのrenderコールバックの中で行うこと
-- (そうしないと次にその領域がdirtyになった瞬間に消える)。
local hello_img = pico.image_load("/img/hello.pimg")
local img_w, img_h = 48, 24 -- 読み込みに失敗した場合のフォールバック値
if hello_img then
    img_w, img_h = pico.image_size(hello_img)
end

local image_canvas = pico.create("Canvas")
pico.set(image_canvas, "x", x + margin)
pico.set(image_canvas, "y", y + margin + 190)
pico.set(image_canvas, "w", img_w)
pico.set(image_canvas, "h", img_h)
pico.on(image_canvas, "render", function()
    if hello_img then
        pico.draw_image(hello_img, x + margin, y + margin + 190)
    end
end)

local back_button = pico.create("Button")
pico.set(back_button, "x", x + margin)
pico.set(back_button, "y", y + margin + 120)
pico.set(back_button, "w", 80)
pico.set(back_button, "h", 30)
pico.set(back_button, "text", "戻る")
pico.on(back_button, "press_start", function()
    -- 呼び忘れてもLuaEngine破棄(onExit())で自動回収されるが、使い終わった
    -- タイミングが分かっているならこうして早めに解放できる、という例
    if hello_img then
        pico.image_free(hello_img)
        hello_img = nil
    end
    pico.pop()
end)

-- シーン制御のデモ: pico.push_scene()は別のLuaスクリプトへ画面遷移する
-- (SceneFunctions::Pushの薄いラッパー。戻り先はスタックへ積まれるのでpico.pop()で戻れる)。
-- 続きはhello_sub.lua/hello_sub2.lua(pico.change_scene()/pico.launch_app()の実演)へ
local sub_button = pico.create("Button")
pico.set(sub_button, "x", x + margin)
pico.set(sub_button, "y", y + margin + 230)
pico.set(sub_button, "w", 100)
pico.set(sub_button, "h", 30)
pico.set(sub_button, "text", "サブ画面へ")
pico.on(sub_button, "press_start", function()
    pico.push_scene("/lua/hello_sub.lua")
end)

local elapsed_ms = 0
local last_shown_sec = -1
local elapsed_label = pico.create("Label")
pico.set(elapsed_label, "x", x + margin)
pico.set(elapsed_label, "y", y + margin + 160)
pico.set(elapsed_label, "text", "loop: 0s")

-- 直接描画のデモ: pico.draw_*/fill_*系はウィジェットを介さずOSData::frameへ直接
-- 描くが、loop()やコールバックから素で呼んでも表示は持続しない(次にその領域が
-- dirtyになった瞬間に背景色で消される)。正しく持続させるには
-- pico.create("Canvas") に乗せ、pico.on(id, "render", fn) で登録した
-- コールバックの中から描く。render()はFlushDirty()の合成サイクルの中で
-- 呼ばれるので、そのたび全部を描き直せば正しく生き残る。
local canvas = pico.create("Canvas")
pico.set(canvas, "x", x + w - 100)
pico.set(canvas, "y", y + margin)
pico.set(canvas, "w", 90)
pico.set(canvas, "h", 90)
pico.on(canvas, "render", function()
    local BLACK = 0
    local cx = x + w - 55
    local cy = y + margin + 40
    pico.draw_circle(cx, cy, 28, BLACK)                                 -- 輪郭
    pico.fill_circle(cx - 10, cy - 8, 3, BLACK)                         -- 左目
    pico.fill_circle(cx + 10, cy - 8, 3, BLACK)                         -- 右目
    pico.draw_line(cx - 12, cy + 10, cx + 12, cy + 10, BLACK)           -- 口
    pico.draw_text(cx - 30, cy + 34, "canvas", BLACK)
end)
-- 生成直後のウィジェットは初期状態でdirtyなので、上のrenderコールバックは
-- 追加の操作なしで次のFlushDirty()で一度呼ばれる(静的な内容はこれで十分)。
-- 内容を変えて描き直したい場合は pico.invalidate(canvas) を呼ぶ。

function setup()
    pico.log("hello.lua: setup()実行")
end

function loop(dt)
    elapsed_ms = elapsed_ms + dt
    local sec = math.floor(elapsed_ms / 1000)
    if sec ~= last_shown_sec then
        -- 毎フレーム同じ文字列でsetTextしても再描画は起きないが(Label側の
        -- 変化なしガード)、1秒に1回だけ計算すれば十分なのでここで間引く
        last_shown_sec = sec
        pico.set(elapsed_label, "text", "loop: " .. sec .. "s")
    end
end

pico.log("hello.lua: 起動しました")
