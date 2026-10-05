---
title: "イベント対応表"
weight: 30
description: "pico.on(id, event_name, fn) に渡せるイベント名と対応ウィジェット"
---

使い方や典型パターンは [イベント](../../guide/events/) を参照してください。ここでは対応表のみ示します。

| event_name | 対応ウィジェット | コールバック引数 | 発生条件 |
|---|---|---|---|
| `press_start` | 全種別 | `(id, x, y, lx, ly, dx, dy)` | 押し始め |
| `press_end` | 全種別 | `(id, x, y, lx, ly, dx, dy)` | 押した場所の上で離した |
| `press_move` | 全種別 | `(id, x, y, lx, ly, dx, dy)` | 押しながら移動 |
| `press_out` | 全種別 | `(id, x, y, lx, ly, dx, dy)` | 押したまま当たり判定の外へ |
| `render` | `Canvas` のみ | `(id)` | `FlushDirty()`の合成サイクル中(dirty時) |
| `closed` | ダイアログ(`pico.show_*`が返すID)のみ | `(id, is_ok)` | ダイアログが閉じた |
| `checked_changed` | `Checkbox` のみ | `(id)` | チェック状態が変わった |
| `value_changed` | `NumberSlider` のみ | `(id)` | 値が変わった(ドラッグ中は毎フレーム) |
| `select_item` | `ScrollList` のみ | `(id, already_selected)` | 項目をタップした |
| `tab_changed` | `TabBar` のみ | `(id)` | 選択タブが変わった(同じタブの押し直しでは発火しない) |
| `dropdown_changed` | `DropdownMenu` のみ | `(id)` | 項目を選んで確定した |
| `text_changed` | `Textbox` のみ | `(id)` | オンスクリーンキーボードを閉じて確定した(1文字ごとには発火しない) |

対応外のウィジェット種別へ登録しようとすると、`pico.on()` はエラーになります。

タッチのイベント(`press_*`)には座標が引数で届きます(`x, y`=画面座標、`lx, ly`=ウィジェット内の座標、`dx, dy`=前のイベントからの移動量。[イベント](../../guide/events/) 参照)。他のイベントには座標は含まれません。必要なら [`pico.get_touch()`](../../api/touch/) を呼んでください。
