---
title: "シーン制御(画面遷移)"
weight: 70
description: "別のLuaスクリプトや他のアプリへ遷移する"
---

## 一覧

```lua
pico.pop()                 -- 自分を起動した画面へ戻る
pico.push_scene(path)      -- 別のLuaスクリプトへ遷移(戻り先をスタックへ積む)
pico.change_scene(path)    -- 別のLuaスクリプトへ遷移(スタックを消費しない)
pico.launch_app(name)      -- ランチャの登録簿にある任意のアプリ(C++製含む)へ遷移
pico.content_rect()        -- x, y, w, h (ステータスバーを除いた描画可能領域)
```

いずれも**要求を登録するだけ**で、実際の画面遷移は次のフレーム境界まで保留されます。呼び出した直後に今のスクリプトの実行が中断されるわけではないので、呼び出し後に後片付けのコードを続けて書けます。

## push_scene vs change_scene vs pop

- `push_scene(path)` は今の画面をスタックへ退避してから新しい画面を開きます。新しい画面で `pico.pop()` すると、`push_scene` を呼んだ画面へ戻ります。
- `change_scene(path)` はスタックを消費せず、今の画面をその場で置き換えます。`change_scene` で開いた画面から `pico.pop()` すると、**`change_scene` を呼んだ画面を飛び越して**、その手前の画面へ直接戻ります。
- `pico.pop()` は「自分を起動した側(ランチャ、または `push_scene` の呼び出し元)」へ戻るための唯一の手段です。

```
ランチャ --push_scene--> A --change_scene--> B --pop--> ランチャ
```
(Aは `change_scene` で捨てられているため、Bから戻る先はAではなくランチャ)

## LuaSceneはPopで戻ると最初から実行し直す

`push_scene` で開いた画面から `pico.pop()` で戻ってきたとき、戻った先の `LuaScene` は**スクリプトを最初から実行し直します**。Lua変数に持たせた状態は画面を出るたびにリセットされます。状態を残したい場合はSDへ保存してください([SDカードアクセス](../sdcard/) 参照)。

## トップレベルで呼ばない

```lua
-- NG: この画面に戻ってくるたびに無条件で発火してしまう
pico.launch_app("電卓")

-- OK: ボタン等のイベント越しに呼ぶ
pico.on(button, "press_start", function()
    pico.launch_app("電卓")
end)
```

`push_scene` / `change_scene` も同じ理由でボタン等のコールバック越しに呼ぶのが安全です。

## launch_app

C++製アプリを含む、ランチャの登録簿にある任意のアプリへ遷移します。

```lua
local ok = pico.launch_app("電卓")
if not ok then
    pico.show_error("アプリが見つかりませんでした")
end
```

アプリ名が登録簿に見つからなければ `false` を返します(綴りミスに気付けるようにするための戻り値)。名前が一致すれば `true` を返しますが、実際の画面遷移自体の成否(スタック上限など)までは判定していません。

## 権限の引き継ぎ

`push_scene` / `change_scene` は、今の `LuaEngine` が持つ権限(`LuaPermissions`)をそのまま新しい画面へ引き継ぎます。複数画面のLuaアプリを作る場合、2画面目以降だけ権限が最小権限に落ちることはありません。一方 `launch_app` は遷移先のアプリ自身の権限に委ねます(独立したアプリへの遷移のため)。詳細は [権限モデル](../permissions/) を参照してください。

## content_rect

```lua
local x, y, w, h = pico.content_rect()
```

ステータスバーを避けた、C++製アプリと同じ配置基準の矩形を返します。ウィジェットの初期配置は基本的にこれを起点にします。
