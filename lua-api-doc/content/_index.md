---
title: "pico-os Lua API"
---

`pico-os` は Raspberry Pi Pico 2 W (RP2350) 上で動く自作タッチGUI OSです。本OSには C++ 実装の `LuaEngine` / `LuaScene` によって、SDカードに置いた Lua スクリプトだけでタッチGUIアプリを作れる仕組みが組み込まれています。

このサイトは `pico.*` として公開されている Lua API のリファレンスと、実際に動くサンプルをまとめたものです。C++ 側の実装詳細やOS全体のアーキテクチャではなく、**Luaでアプリを書く人**の視点で構成しています。
