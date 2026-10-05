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
| `closed` | ダイアログ(`pico.show_*`が返すID)のみ | `(id, is_ok, value)` | ダイアログが閉じた。決定で閉じたときは`value`に結果が入る([ダイアログ](../../api/dialogs/)) |
| `checked_changed` | `Checkbox` のみ | `(id)` | チェック状態が変わった |
| `value_changed` | `NumberSlider` のみ | `(id)` | 値が変わった(ドラッグ中は毎フレーム) |
| `select_item` | `ScrollList` のみ | `(id, already_selected)` | 項目をタップした |
| `tab_changed` | `TabBar` のみ | `(id)` | 選択タブが変わった(同じタブの押し直しでは発火しない) |
| `dropdown_changed` | `DropdownMenu` のみ | `(id)` | 項目を選んで確定した |
| `text_changed` | `Textbox` のみ | `(id)` | オンスクリーンキーボードを閉じて確定した(1文字ごとには発火しない) |
| `text_input` | `Textbox` のみ | `(id)` | 1文字ごと(入力中も`text`が最新になる) |
| `duration_changed` | `DurationPicker` のみ | `(id, total_ms)` | ▲▼で値が変わった(`pico.set`では発火しない) |
| `day_selected` | `MonthGrid` のみ | `(id, day)` | 日付をタップした(同じ日の押し直しでも発火する) |
| `link_tap` | `MarkdownView` のみ | `(id, path)` | リンクをタップした(`path`は文書基準で解決済みのパスかURL) |
| `text_tap` | `TextView` のみ | `(id, byte_offset)` | 動かさずにタップした(位置は文章中のバイト位置) |
| `scrolled` | `ScrollContainer` のみ | `(id, scroll_y)` | スクロール位置が変わった |
| `long_press` | 全種別 | `(id, x, y, lx, ly)` | 動かさずに0.5秒押し続けた |
| `double_tap` | 全種別 | `(id, x, y, lx, ly)` | 0.4秒以内に近い位置で2回タップした |
| `swipe` | 全種別 | `(id, direction, dx, dy, x, y)` | 24px以上を0.7秒以内に動かして離した。`direction`は`"left"` `"right"` `"up"` `"down"` |

対応外のウィジェット種別へ登録しようとすると、`pico.on()` はエラーになります。

タッチのイベント(`press_*`)には座標が引数で届きます(`x, y`=画面座標、`lx, ly`=ウィジェット内の座標、`dx, dy`=前のイベントからの移動量。[イベント](../../guide/events/) 参照)。他のイベントには座標は含まれません。必要なら [`pico.get_touch()`](../../api/touch/) を呼んでください。
