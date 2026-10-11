---
title: "アプリストアで配る"
weight: 120
description: "app.cfg の id と version、署名した配布物の作り方、本体のストアでのインストール"
---

作ったLuaアプリは、PCでSDカードへコピーする代わりに**ランチャの「ストア」から Web 経由で入れる**こともできます。
配布物の形式は リポジトリの `APP_REPO.md`、作る道具は `script/build_app_repo.py` です。

## アプリ側で用意するもの

`app.cfg` に **`id`**(英小文字・数字・`-`・`_`、23文字まで)と **`version`**(`1.2.0` のような数字と `.`)を書きます。

```
name=テトリス
id=tetris
version=2.0.0
icon=icon.pimg
permission_network=false
```

- ファイル名は英数字と `. - _` だけ(日本語のファイル名は配れません)。ディレクトリ名(`/lua/apps/<ここ>`)は日本語でかまいません。
- 版を上げるときは `version` を増やします。本体は「入っている版 < 一覧の版」なら「更新」を出します。
- 権限(`permission_network` / `permission_notify` / `permission_sd_outside_app_dir`)は一覧にも載り、入れる前に利用者へ見せます。
- `pico.store_save()` のセーブや `pico.sd_write()` で自分のディレクトリに書いたファイルは、**更新しても残ります**(削除すると消えます)。

## 配布物を作る

```sh
# 鍵(1回だけ)。秘密鍵は手元のPCの外へ出さない
python3 script/build_app_repo.py keygen --key ~/.pico-os/app_repo.key

# 配布物をまるごと作る(Raspberry Pi などの静的なサーバへ置く)
python3 script/build_app_repo.py build --key ~/.pico-os/app_repo.key --out /tmp/apps --clean
```

表示された公開鍵を本体のSDの `/sys/store.cfg` に `key = ...` として書くか、`src/store/App_Repo_Keys.hpp` へ足してファームに焼き込みます。
配布元のURLは `/sys/store.cfg` の `url = ...`(ストアの [設定] でも変えられます)。

GitHub Pages で配るときは、署名した目録だけを `store/` にコミットします(アプリの中身は公開のワークフローが写します)。

```sh
python3 script/build_app_repo.py sign --key ~/.pico-os/app_repo.key --out store
```

## 本体での動き

- 一覧は署名を確かめてから使います。署名が合わない・信頼していない鍵・前より古い一覧は使いません。
- 入れるときは、ファイルを1つずつSHA-256で確かめ、`app.cfg` の `id`・版・権限が一覧と同じかを見てから `/lua/apps/<dir>/` へ移します。
- 入れたアプリはすぐランチャに並びます(再起動は要りません)。
