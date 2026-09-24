---
title: "ペイント"
weight: 30
description: "pc/sdcard/lua/apps/ペイント/main.lua — CanvasRasterとダイアログで作るお絵描きアプリ"
---

ペン・消しゴム・直線・四角形・楕円・塗りつぶし(バケツ)で描き、`.pimg` で保存/読込するアプリです。SDスキャンで自動登録されます(`app.cfg` と `icon.pimg` 付き)。

描画の中身(ドラッグ中の図形のプレビュー、焼き込み、塗りつぶし、元に戻す)は全て `CanvasRaster` 側の仕事で、スクリプトは道具の切り替えとファイル操作の配線だけを持ちます。

## 画面

```
[←][ペン][消しゴム][直線][四角][楕円][バケツ]   ← 道具(選択中は赤枠)
[色 ][太さ][元に戻す][新規][開く][保存]          ← 操作
無題(未保存)  ペン                              ← ステータス行
┌──────────────────────────┐
│        CanvasRaster        │
└──────────────────────────┘
```

- **四角形/楕円は、選択中にもう一度押すと「輪郭」⇔「塗りつぶし」が切り替わります**(アイコンも塗りつぶし版に変わる)。
- 色ボタンは今の色そのものを背景にし、太さボタンの上には今の太さと色の丸を `Canvas` で描いています。

## 道具とキャンバスの設定

```lua
pico.set(canvas, "undo_enabled", true)        -- 元に戻す用の控えを持つ
pico.set(canvas, "canvas_mode", 5)            -- 0=ペン 1=四角 2=楕円 4=直線 5=塗りつぶし
pico.set(canvas, "filled", true)              -- 四角/楕円を塗りつぶす
pico.set(canvas, "color", 12)                 -- 消しゴムは color=15(白)のペン
pico.set(canvas, "brush_radius", 2)
```

## ダイアログの使い方

| 操作 | ダイアログ |
|---|---|
| 色 | `pico.show_color()` → `pico.get(id, "value")` |
| 保存 | `pico.show_file_save(dir, "無題.pimg")`。既存のファイルへ上書きするときは `pico.show_message()` で確認 |
| 開く | `pico.show_file_select(dir)` → `pico.canvas_load(canvas, path, true)`(キャンバスの大きさを保って読む) |
| 新規・未保存での終了/開く | `pico.show_message()` で確認 |

**ダイアログを閉じた直後に次のダイアログを開くと、閉じたほうの跡が画面に残ります**(半透明のダイアログの下は描き直されないため)。そこで `closed` の中では次を直接開かず、`loop()` で数フレーム待ってから開いています。

```lua
local pending, pending_frames = nil, 0
local function later(fn) pending = fn; pending_frames = 2 end

function loop(dt)
    if pending then
        pending_frames = pending_frames - 1
        if pending_frames <= 0 then
            local fn = pending
            pending = nil
            fn()
        end
    end
end

local d = pico.show_message("白紙に戻しますか?", "キャンセル", "白紙に")
pico.on(d, "closed", function(_, is_ok)
    if is_ok then later(function() pico.canvas_clear(canvas) end) end
end)
```

## 保存先

SDスキャンで登録されたアプリは `sd_outside_app_dir` 権限を持たないので、保存/読込は `/lua/apps/ペイント/` の中だけです(外を選ぶと失敗のダイアログが出ます)。

## 注意: スクリプトの大きさ

`LuaScene` が読み込むスクリプトは16KiBまでです(超えると切り詰められて構文エラーになる)。日本語のコメントは1文字3バイトなので、大きめのアプリではコメントの量に気をつけてください。
