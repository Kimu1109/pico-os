---
title: "コントローラー"
weight: 27
description: "pad_connected / pad_down / pad_pressed / pad_released"
---

外部コントローラー(ゲームパッド)のボタンを読みます。タッチは同時に1点しか取れませんが、コントローラーなら「十字キーを押しながらA」のような**同時押し**ができます。

今の入力元は**USBシリアル経由のPCのキーボード**です(PCで `script/pad_serial.py` を動かします。下記)。実物のコントローラーに対応しても、スクリプトはそのまま動きます。

- 状態は**フレームの頭で1回だけ**更新されます。`loop(dt)` の中で何度読んでも同じ答えです。
- コントローラーがつながっていなければ、どのボタンも押していない扱いです(エラーにはなりません)。
- ボタン名を間違えるとエラーになります(綴りの間違いで「押しても反応しない」と悩まないように)。

## ボタン名

| 名前 | ボタン | PCのキー(`pad_serial.py`) |
|---|---|---|
| `"up"` `"down"` `"left"` `"right"` | 十字キー | 矢印キー |
| `"a"` `"b"` | A / B | X / Z |
| `"x"` `"y"` | X / Y | S / A |
| `"l"` `"r"` `"zl"` `"zr"` | 肩のボタン | Q / W / E / R |
| `"start"` `"select"` | START(+) / SELECT(−) | Enter / BackSpace(右Shift) |
| `"home"` | HOME | H / Esc |

並びはWiiクラシックコントローラーに合わせています(右がA、下がB、左がY、上がX)。

## pico.pad_connected

<div class="sig">pico.pad_connected() <span class="ret">-> boolean</span></div>

コントローラーがつながっているか。USBシリアルの場合は、PC側のスクリプトから0.5秒以内に状態が届いていれば `true` です。

## pico.pad_down

<div class="sig">pico.pad_down(name: string) <span class="ret">-> boolean</span></div>

そのボタンを今押しているか。移動のように「押している間ずっと」効かせたいものに使います。

## pico.pad_pressed / pico.pad_released

<div class="sig">pico.pad_pressed(name: string) <span class="ret">-> boolean</span></div>
<div class="sig">pico.pad_released(name: string) <span class="ret">-> boolean</span></div>

そのボタンを**このフレームで**押した / 離したか。次のフレームでは `false` に戻ります。ジャンプや決定のように「1回押したら1回だけ」効かせたいものに使います。

## 使用例

```lua
local px, py = 100, 100

function loop(dt)
    local step = 80 * dt / 1000   -- 80px/秒
    if pico.pad_down("left")  then px = px - step end
    if pico.pad_down("right") then px = px + step end
    if pico.pad_down("up")    then py = py - step end
    if pico.pad_down("down")  then py = py + step end

    if pico.pad_pressed("a") then pico.beep(880, 50) end
    if pico.pad_pressed("home") then pico.pop() end
end
```

ランチャの「コントローラー確認」(`/lua/apps/コントローラー確認/main.lua`)が、押しているボタンを図で出す実例です。

## PCのキーボードをコントローラーにする

```sh
pip install pyserial                        # Windowsでは必須。Linux/macOSは無くても動く
python3 script/pad_serial.py --list         # ポートの一覧
python3 script/pad_serial.py COM3           # 小さなウィンドウが開く。そこでキーを押す
```

- ポートは同時に1つのプログラムしか開けないので、シリアルモニタとは同時に使えません。代わりに、pico-osのログがこのスクリプトのターミナルへそのまま出ます。
- PCビルドでは標準入力がシリアルの代わりです: `python3 script/pad_serial.py --stdout | ./pc/build/picoos_pc`
- Web版では使えません(シリアルも標準入力もありません)。
