---
title: "権限モデル"
weight: 100
description: "network / sd_outside_app_dir の2値パーミッション"
---

Luaアプリへ許す操作は、all-or-nothingの2つのフラグで決まります。既定は**両方 `false`(最小権限)**です。

```cpp
struct LuaPermissions {
    bool network = false;
    bool sd_outside_app_dir = false;
};
```

| フラグ | 効果がある関数 | `false`のときの制限 |
|---|---|---|
| `network` | `pico.http_request` / `pico.http_cancel` | 呼んでも `false` が返るだけ(エラーにはならない) |
| `sd_outside_app_dir` | `pico.sd_exists/read/write/remove/mkdir/list` / `pico.image_load` | そのアプリの `app_dir`(通常は自分のスクリプトの親ディレクトリ)の配下しかアクセスできない |

権限は**Luaスクリプトが構築されるとき(=画面が開かれるとき)に1回だけ**決まります。実行中にスクリプト側から変更する手段はありません。パスのホワイトリストやホスト単位の細かい制限は今のところ無く、「持ち場の外へ出られるか出られないか」だけを見る、最初の一歩としての粗い実装です。

## 誰が権限を決めるか

### SD走査で自動登録されたアプリ(`/lua/apps/<名前>/main.lua`)

**常に既定値(両方 `false`)です。** 中身を検証していないスクリプトへ、走査した側が勝手に強い権限を与えることはありません。ネットワークやアプリディレクトリ外へのアクセスが必要なアプリは、この方法では登録できません。

### C++側に手動登録したアプリ

`src/functions/App_List.cpp` の `Register()` 呼び出し側が明示的に権限を決めます。

```cpp
Scene* MakeMyLuaAppScene(const AppEntry& entry) {
    LuaPermissions permissions;
    permissions.network = true;
    permissions.sd_outside_app_dir = true;
    return new LuaScene(entry.arg.c_str(), permissions);
}

Register("My App", IconID::AppBox, &MakeMyLuaAppScene, "/lua/my_app.lua");
```

## app_dir とは

`sd_outside_app_dir == false` の間、SD操作は `app_dir` 配下だけに閉じ込められます。`app_dir` は通常、そのLuaスクリプト自身の親ディレクトリです。

- `/lua/apps/myapp/main.lua` を実行している場合、`app_dir` は `/lua/apps/myapp` になります。
- `pico.push_scene()` / `pico.change_scene()` で別のスクリプトへ移った場合、`app_dir` は**遷移先スクリプト自身の親ディレクトリ**として計算し直されます(1つ前の画面の `app_dir` を引きずりません)。

```lua
-- app_dir が "/lua/apps/myapp" の場合
pico.sd_read("/lua/apps/myapp/save.txt")        -- OK
pico.sd_read("/lua/apps/myapp/sub/data.txt")    -- OK(配下のさらに下も可)
pico.sd_read("/lua/other_app/save.txt")         -- 拒否(nilが返り、警告ログが出る)
```

## 権限は「アプリ単位」で引き継がれる

`pico.push_scene()` / `pico.change_scene()` で複数画面のLuaアプリを作った場合、**画面が変わっても権限(`network` / `sd_outside_app_dir`)はそのまま引き継がれます**。2画面目以降だけ権限が最小権限に落ちることはありません。一方 `pico.launch_app()` で別のアプリへ遷移した場合は、遷移先アプリ自身の権限に切り替わります(独立したアプリとして扱われるため)。
