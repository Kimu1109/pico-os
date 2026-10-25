---
aliases: ["/guide/publishing/"]
title: "アプリストアで配る"
weight: 40
description: "app.cfg の id と version、署名した配布物の作り方、本体のストアやSDに置くパッケージでのインストール"
---

作ったLuaアプリは、SDカードへコピーする代わりに**ランチャの「ストア」から Web 経由で入れる**こともできます。
配布物を作る道具はリポジトリの `script/build_app_repo.py`、形式の細かい決まりは `docs/APP_REPO.md` です。

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
- 権限(`permission_*`)は一覧にも載り、入れる前に利用者へ見せます。
- `pico.store_save()` のセーブや `pico.sd_write()` で自分のディレクトリに書いたファイルは、**更新しても残ります**(削除すると消えます)。

## 配布物を作る

```sh
# 鍵(1回だけ)。秘密鍵は手元のPCの外へ出さない
python3 script/build_app_repo.py keygen --key ~/.pico-os/app_repo.key

# 配布物をまるごと作る(Raspberry Pi などの静的なサーバへ置く)
python3 script/build_app_repo.py build --key ~/.pico-os/app_repo.key --out /tmp/apps --clean
```

表示された公開鍵を、入れる側の本体のSDの `/sys/store.cfg` に `key = ...` として書いてもらいます。
配布元のURLは `/sys/store.cfg` の `url = ...` です(ストアの [設定] でも変えられます)。

pico-os のリポジトリの GitHub Pages で配るときは、署名した目録だけを `store/` にコミットします(アプリの中身は公開のワークフローが写します)。

```sh
python3 script/build_app_repo.py sign --key ~/.pico-os/app_repo.key --out store
```

## 本体での動き

- 一覧は署名を確かめてから使います。署名が合わない・信頼していない鍵・前より古い一覧は使いません。
- 入れるときは、ファイルを1つずつSHA-256で確かめ、`app.cfg` の `id`・版・権限が一覧と同じかを見てから `/lua/apps/<dir>/` へ移します。
- 入れたアプリはすぐランチャに並びます(再起動は要りません)。

## SDカードに置いて入れる(パッケージ)

Wi-Fiが無くても入れられるように、アプリを1つのファイル(`.papp`)にまとめたパッケージもあります。

- [アプリストアのページ](https://kimu1109.github.io/pico-os/apps/)の **DOWNLOAD** で落としたファイルを、**SDカードの直下**にそのまま置きます。
  次に起動したとき(またはUSBドライブを終えたとき)に `/lua/apps/<dir>/` へ入り、通知が出ます。入ったファイルは消えます。
- 入れられなかったときは、ファイルの名前の後ろに `.err` が付き、通知に理由が出ます。
- ストアから入れるときと同じく、作者の署名とファイルごとのSHA-256を確かめます。一度入れたアプリは、同じ作者の鍵で署名された新しい版でしか更新できません。

ストアに載せずに自分のアプリを渡したいときは、作者の鍵でパッケージを作ります。

```sh
python3 script/build_app_repo.py keygen --key ~/.pico-os/author.key      # 1回だけ
python3 script/build_app_repo.py package --key ~/.pico-os/author.key --app pc/sdcard/lua/apps/テトリス
# → tetris-2.0.0.papp(app.cfg の id と version から名前を付ける)
```

配布物を自分のサーバに置くときは、`build` の後に `site` を回すと、同じ場所に一覧のページ(`index.html`)とパッケージができます。

```sh
python3 script/build_app_repo.py site --dir /tmp/apps
```
