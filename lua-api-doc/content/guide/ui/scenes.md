---
aliases: ["/guide/scenes/"]
title: "画面遷移"
weight: 70
description: "別のLuaスクリプトの画面や、他のアプリへ移る"
---

## 一覧

```lua
pico.pop([result])               -- 自分を開いた画面へ戻る
pico.push_scene(path [, args])   -- 別のLuaスクリプトの画面を開く(戻ってこられる)
pico.change_scene(path [, args]) -- 今の画面を別のLuaスクリプトに置き換える
pico.launch_app(name)            -- ランチャにある別のアプリを開く
```

どれも**頼むだけ**で、実際に画面が変わるのはそのフレームの終わりです。呼んだ後のコードもそのまま実行されるので、後片付けを続けて書けます。

## push_scene・change_scene・pop

- `push_scene(path)` は今の画面を残したまま新しい画面を開きます。新しい画面で `pico.pop()` すると、元の画面へ戻ります。
- `change_scene(path)` は今の画面を置き換えます。置き換えた画面で `pico.pop()` すると、**置き換えられた画面を飛ばして**、その前の画面へ戻ります。
- `pico.pop()` は、自分を開いた画面(ランチャ、または `push_scene` を呼んだ画面)へ戻ります。

```
ランチャ --push_scene--> A --change_scene--> B --pop--> ランチャ
```

(Aは `change_scene` で置き換えられたので、Bから戻る先はランチャ)

`path` はSDの絶対パスです。同じアプリの別のファイルなら `pico.path_join(pico.app_dir(), "settings.lua")` と書きます。開いた先でもアプリのフォルダ・権限・`require` はそのまま同じです。

## 戻ってくると最初から実行し直す

`push_scene` で開いた画面から戻ってくると、元の画面のスクリプトは**最初から実行し直されます**。変数に持っていた状態は消えるので、残したいものは次のどれかで受け渡します。

| やりたいこと | 使うもの |
|---|---|
| 開く画面に値を渡す | `push_scene(path, args)` → 開いた画面で `pico.args()` |
| 開いた画面から結果を受け取る | 開いた画面で `pico.pop(result)` → 元の画面の `on_result(result)` |
| 離れる前の状態を戻ってから復元する | `on_suspend()` が返したテーブル → `on_resume(state)` |
| アプリを閉じても残す | `pico.store_save()` / `pico.store_load()`、またはSDへ保存 |

書き方の例は [画面遷移API](../../../api/ui/scenes/#画面の間で値を受け渡す) にあります。

## トップレベルで呼ばない

```lua
-- だめ: この画面に戻ってくるたびに(最初から実行し直すので)すぐ移ってしまう
pico.launch_app("電卓")

-- よい: ボタンなどのイベントから呼ぶ
pico.on(button, "press_start", function()
    pico.launch_app("電卓")
end)
```

`push_scene` / `change_scene` も同じ理由で、イベントから呼んでください。

## launch_app

ランチャにある別のアプリ(OS標準のアプリを含む)を名前で開きます。

```lua
if not pico.launch_app("電卓") then
    pico.show_error("アプリが見つかりませんでした")
end
```

名前が見つからなければ `false` を返します。開いたアプリは、そのアプリ自身の権限で動きます。

## ステータスバーを避ける

```lua
local x, y, w, h = pico.content_rect()
```

画面上部のステータスバーを除いた、アプリが使える範囲を返します。ウィジェットの配置はこれを基準にします。
