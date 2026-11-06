# 同梱Luaアプリのアイコンの元絵

`sdcard/lua/apps/<名前>/icon.pimg` の元になったPNG(48x48)。ファイル名はアプリのフォルダ名と同じにする。
アプリのフォルダに置かないのは、ストアで配る中身(`app/` の全部)にPNGが入ってしまうため。

描き直したら、透過を付けずに変換して差し替える(今の `icon.pimg` はこの変換とバイト単位で同じ):

```sh
picoos pimg script/assets/app_icons/リバーシ.png sdcard/lua/apps/リバーシ/icon.pimg
```

アプリの版を上げてから公開すること(`docs/design/lua-apps.md`「アプリストアの投稿」)。

| 元絵 | アイコン |
|---|---|
| `ブロック崩し.png` | `sdcard/lua/apps/ブロック崩し/icon.pimg` |
| `マインスイーパー.png` | `sdcard/lua/apps/マインスイーパー/icon.pimg` |
| `リバーシ.png` | `sdcard/lua/apps/リバーシ/icon.pimg` |

ほかのアプリのアイコンはプログラムで描いている(`script/assets/generate_*_icon.py` 等)か、元絵が残っていない。
元絵が `script/secondary-icons/` にあった頃は参照が無く、何の絵か分からなかったのでここへ移した(2026-11-06)。
