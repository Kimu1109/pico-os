---
title: "時刻"
weight: 80
description: "get_time"
---

## pico.get_time

<div class="sig">pico.get_time() <span class="ret">-> t: table</span></div>

現在時刻を1個のテーブルで返します。

```lua
local t = pico.get_time()
-- t.year, t.month, t.day, t.hour, t.min, t.sec, t.wday
print(string.format("%04d-%02d-%02d %02d:%02d:%02d",
    t.year, t.month, t.day, t.hour, t.min, t.sec))
```

| フィールド | 説明 |
|---|---|
| `year` | 西暦(例: `2026`) |
| `month` | 月(`1`〜`12`) |
| `day` | 日 |
| `hour` | 時(0〜23) |
| `min` | 分 |
| `sec` | 秒 |
| `wday` | 曜日。`0`=日曜 〜 `6`=土曜 |

NTP同期が完了する前は、値の妥当性は保証されません(OS起動直後の初期値がそのまま返ることがあります)。
