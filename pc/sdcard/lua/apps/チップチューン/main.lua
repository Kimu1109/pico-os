-- チップチューン音源の動作確認。
-- 鍵盤(1オクターブ+1音)を押すとチャンネル1で鳴る。波形と減衰はボタンで切り替え。
-- 「デモ曲」は4チャンネル(メロディ/アルペジオ/ベース/ドラム)の短いループをloop(dt)で並べて鳴らす。
-- アンプがつながっていなくても同じように動く(音が出ないだけ。上の1行で分かる)。

local cx, cy, cw, ch = pico.content_rect()

local WAVES = { "pulse50", "pulse25", "pulse12", "triangle", "saw", "noise", "noise_short" }
local wave_index = 1
local decay = true

-- ---- 上の段: 戻る / 波形 / 減衰 ----
local function button(x, y, w, text, fn)
    local b = pico.create("Button")
    -- 文字を先に決める(後から文字を変えると箱の大きさが測り直される)
    pico.set(b, "font_size", 0); pico.set(b, "text", text)
    pico.set(b, "x", x); pico.set(b, "y", y)
    pico.set(b, "w", w); pico.set(b, "h", 18)
    pico.on(b, "press_start", fn)
    return b
end

button(cx + 2, cy + 2, 40, "戻る", function() pico.pop() end)

local wave_btn
wave_btn = button(cx + 46, cy + 2, 104, "波形: " .. WAVES[wave_index], function()
    wave_index = wave_index % #WAVES + 1
    pico.set(wave_btn, "text", "波形: " .. WAVES[wave_index])
end)

local decay_btn
decay_btn = button(cx + 154, cy + 2, cw - 156, "減衰: あり", function()
    decay = not decay
    pico.set(decay_btn, "text", decay and "減衰: あり" or "減衰: なし")
end)

-- ボタンは"h"の外側に余白と影(約10px)を描くので、その分空けて下を並べる
local row_bottom = cy + 2 + pico.get(wave_btn, "h") + 10

local status = pico.create("Label")
pico.set(status, "x", cx + 4); pico.set(status, "y", row_bottom + 4)
pico.set(status, "font_size", 0)

-- ---- 鍵盤 ----
-- C4〜C5の白鍵8つ。黒鍵はC#/D#/F#/G#/A#
local KEY_TOP = row_bottom + 24
local KEY_H = 120
local BLACK_H = 72
local WHITE_W = math.floor(cw / 8)
local BLACK_W = math.floor(WHITE_W * 0.6)
local WHITE_NOTES = { 60, 62, 64, 65, 67, 69, 71, 72 }
-- 黒鍵: {白鍵の何番目の右端にあるか, ノート番号}
local BLACK_KEYS = { {1, 61}, {2, 63}, {4, 66}, {5, 68}, {6, 70} }
local NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }

local pressed_note = nil

local function blackRect(i)
    local k = BLACK_KEYS[i]
    return cx + k[1] * WHITE_W - math.floor(BLACK_W / 2), KEY_TOP, BLACK_W, BLACK_H
end

-- 座標 → ノート番号(黒鍵が上に重なっているので先に見る)
local function noteAt(x, y)
    if y < KEY_TOP or y >= KEY_TOP + KEY_H then return nil end
    if y < KEY_TOP + BLACK_H then
        for i = 1, #BLACK_KEYS do
            local bx, _, bw = blackRect(i)
            if x >= bx and x < bx + bw then return BLACK_KEYS[i][2] end
        end
    end
    local i = math.floor((x - cx) / WHITE_W) + 1
    return WHITE_NOTES[i]
end

local keys = pico.create("Canvas")
pico.set(keys, "x", cx); pico.set(keys, "y", KEY_TOP)
pico.set(keys, "w", WHITE_W * 8); pico.set(keys, "h", KEY_H)
pico.on(keys, "render", function()
    for i = 1, 8 do
        local x = cx + (i - 1) * WHITE_W
        if pressed_note == WHITE_NOTES[i] then pico.fill_rect(x, KEY_TOP, WHITE_W, KEY_H, 7) end
        pico.draw_rect(x, KEY_TOP, WHITE_W + 1, KEY_H, 0)
    end
    for i = 1, #BLACK_KEYS do
        local bx, by, bw, bh = blackRect(i)
        pico.fill_rect(bx, by, bw, bh, pressed_note == BLACK_KEYS[i][2] and 8 or 0)
    end
end)

local note_label = pico.create("Label")
pico.set(note_label, "x", cx + 4); pico.set(note_label, "y", KEY_TOP + KEY_H + 6)
pico.set(note_label, "font_size", 0)
pico.set(note_label, "text", "鍵盤を押すと鳴ります")

local function playKey(note)
    if note == pressed_note then return end
    pressed_note = note
    pico.invalidate(keys)
    if not note then
        -- 減衰なしは押している間だけ鳴らす。減衰ありは離しても消えるまで鳴らしておく
        if not decay then pico.sound_stop(1) end
        return
    end
    local wave = WAVES[wave_index]
    local freq = pico.note_freq(note)
    -- ノイズは周波数がそのまま「ザー」の粗さになるので、高めに振っておく
    if wave == "noise" or wave == "noise_short" then freq = freq * 16 end
    pico.sound_play(1, freq, 0, { wave = wave, volume = 15, envelope = decay and -2 or 0 })
    pico.set(note_label, "text", string.format("%s%d  %.1fHz", NAMES[note % 12 + 1], note // 12 - 1, pico.note_freq(note)))
end

local function onTouch()
    local x, y, touched = pico.get_touch()
    if touched then playKey(noteAt(x, y)) else playKey(nil) end
end
pico.on(keys, "press_start", onTouch)
pico.on(keys, "press_move", onTouch)
pico.on(keys, "press_end", function() playKey(nil) end)
pico.on(keys, "press_out", function() playKey(nil) end)

-- ---- デモ曲 ----
-- 1文字=8分音符(150ms)。音名=鳴らす、"-"=前の音を伸ばす、"."=休み
local STEP_MS = 150
local TRACKS = {
    { ch = 1, opts = { wave = "pulse25", volume = 12, envelope = -6 },
      notes = "E5 - G5 - A5 - G5 E5 D5 - E5 - C5 - - - E5 - G5 - A5 - C6 - B5 - G5 - A5 - - -" },
    { ch = 2, opts = { wave = "pulse12", volume = 6, envelope = -2 },
      notes = "C4 E4 G4 E4 A3 C4 E4 C4 F3 A3 C4 A3 G3 B3 D4 B3 C4 E4 G4 E4 A3 C4 E4 C4 F3 A3 G3 B3 C4 E4 G4 E4" },
    { ch = 3, opts = { wave = "triangle", volume = 15 },
      notes = "C3 - C3 - A2 - A2 - F2 - F2 - G2 - G2 - C3 - C3 - A2 - A2 - F2 - G2 - C3 - - -" },
    { ch = 4, drums = true,
      notes = "K h S h K h S h K h S h K h S h K h S h K h S h K h S h K K S S" },
}
-- ドラム: ノイズの粗さ(周波数)と長さで叩き分ける
local DRUMS = {
    K = { 600,  120, { wave = "noise", volume = 13, envelope = -1 } },
    S = { 5000, 140, { wave = "noise", volume = 10, envelope = -1 } },
    h = { 12000, 30, { wave = "noise_short", volume = 4 } },
}

for _, t in ipairs(TRACKS) do
    t.steps = {}
    for tok in t.notes:gmatch("%S+") do t.steps[#t.steps + 1] = tok end
end
local SONG_LEN = #TRACKS[1].steps

local playing = false
local step = 0
local elapsed = 0

local function playStep(s)
    for _, t in ipairs(TRACKS) do
        local tok = t.steps[s]
        if t.drums then
            local d = DRUMS[tok]
            if d then pico.sound_play(t.ch, d[1], d[2], d[3]) end
        elseif tok == "." then
            pico.sound_stop(t.ch)
        elseif tok ~= "-" then
            -- 後ろに続く"-"の数だけ伸ばす(最後は少し切って音の粒を立てる)
            local len = 1
            while t.steps[s + len] == "-" do len = len + 1 end
            pico.sound_play(t.ch, pico.note_freq(tok), len * STEP_MS - 20, t.opts)
        end
    end
end

local song_btn
song_btn = button(cx + 2, KEY_TOP + KEY_H + 26, 130, "デモ曲を再生", function()
    playing = not playing
    if playing then
        step, elapsed = 0, STEP_MS   -- 次のloop()ですぐ1拍目を鳴らす
        pico.set(song_btn, "text", "デモ曲を止める")
    else
        pico.sound_stop()
        pico.set(song_btn, "text", "デモ曲を再生")
    end
end)

local status_timer = 1000
function loop(dt)
    status_timer = status_timer + dt
    if status_timer >= 500 then
        status_timer = 0
        pico.set(status, "text", pico.sound_available() and "音: 鳴らせます"
                                   or "音: アンプ未接続(鳴りません)")
    end

    if not playing then return end
    elapsed = elapsed + dt
    while elapsed >= STEP_MS do
        elapsed = elapsed - STEP_MS
        step = step % SONG_LEN + 1
        playStep(step)
    end
end
