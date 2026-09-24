---
title: "定数・上限一覧"
weight: 40
description: "メモリ予算・実行時間・各種サイズ上限・列挙型の値"
---

## メモリ・実行時間

| 項目 | 値 | 説明 |
|---|---|---|
| Lua stateのメモリ予算 | 200 × 1024 = 204,800 バイト | `LuaScene`が`LuaEngine`を生成する際に渡す上限。state本体+標準ライブラリ+スクリプトの全確保を含む |
| 命令数の上限(1回の呼び出しあたり) | 2,000,000命令 | 超過するとその呼び出しは実行時エラーとして打ち切られる。[実行時間の安全網](../../guide/execution-limits/) 参照 |
| フック間隔 | 1,000命令 | 上の上限を監視する頻度(内部実装の詳細) |
| スクリプト本体の読み込み上限 | 16KiB(16,384バイト) | 超過分は切り詰めて実行(警告ログあり) |

## SD・ネットワーク・画像のサイズ上限

| 項目 | 値 |
|---|---|
| `pico.sd_read()` の読み込み上限 | 16KiB(超過は`nil`。切り詰めない) |
| `pico.http_request()` の送信ボディ上限 | 16KiB |
| `pico.http_request()` の受信本文上限 | 16KiB |
| 同時に保持できる画像(`pico.image_load`)の枚数 | 4枚 |
| 画像データの合計サイズ上限 | 64KiB(65,536バイト) |
| `TabBar` に追加できるタブ数(`pico.tab_add`) | 4個 |

## WidgetIdのビット構成

32bit整数のうち、上位から順に次のように使われています。

| ビット範囲 | 内容 | 意味 |
|---|---|---|
| `[31:26]`(6bit) | 種別(`WidgetType`) | 最大64種別まで表現可能 |
| `[25:10]`(16bit) | 世代(generation) | スロット使い回し時の破棄済みID検出に使う |
| `[9:0]`(10bit) | インデックス | 同時に存在できるウィジェットは最大1,023個 |

`0`(オールゼロ)は常に無効なIDです。

## 列挙型の値

### font_size(FontFn::FontSize)

| 値 | 名前 | 実サイズ |
|---|---|---|
| `0` | Small | 16px |
| `1` | Normal | 24px(既定) |
| `2` | Big | 32px |
| `3` | Bigger | 48px |

### icon_size(IconSize)

| 値 | 実サイズ |
|---|---|
| `0` | 16px(既定) |
| `1` | 24px |
| `2` | 32px |
| `3` | 48px |
| `4` | 64px |

### icon_id(IconID)

`Icon`(`pico.create("Icon")`)と、アイコンボタン化した`Button`(`icon_id`を設定した状態)の`icon_id`に渡す値です。`src/gui/icons/icons_data.h`の`enum class IconID`の並び順(0始まりの連番、明示値なし)そのままです。

> **並び順は将来変わり得ます。** 新しいアイコンは必ず末尾に追加する運用になっていますが、この表はある時点のスナップショットです。数値を確実に合わせたい場合は`src/gui/icons/icons_data.h`(または生成元の`script/generate_icons.py`の`ICONS`リスト)を直接確認してください。

| 値 | 名前 | 値 | 名前 |
|---|---|---|---|
| 0 | `WifiSignal1` | 35 | `CheckboxOff` |
| 1 | `WifiSignal2` | 36 | `CircleDashedPlus` |
| 2 | `WifiSignal3` | 37 | `Refresh` |
| 3 | `WifiSignal4` | 38 | `Power` |
| 4 | `Battery0` | 39 | `Menu` |
| 5 | `Battery1` | 40 | `DotsVertical` |
| 6 | `Battery2` | 41 | `ChevronDown` |
| 7 | `Battery3` | 42 | `ChevronUp` |
| 8 | `Battery4` | 43 | `Star` |
| 9 | `BatteryCharging` | 44 | `Share` |
| 10 | `SdCard` | 45 | `User` |
| 11 | `VolumeHigh` | 46 | `Send` |
| 12 | `VolumeLow` | 47 | `Keyboard` |
| 13 | `VolumeOff` | 48 | `Language` |
| 14 | `Sun0` | 49 | `Link` |
| 15 | `Sun1` | 50 | `Save` |
| 16 | `Sun2` | 51 | `StackPop` |
| 17 | `Sun3` | 52 | `StackPush` |
| 18 | `Bell` | 53 | `Brush` |
| 19 | `BellOff` | 54 | `Eraser` |
| 20 | `BellRinging` | 55 | `AlertTriangle` |
| 21 | `Lock` | 56 | `InfoCircle` |
| 22 | `LockOff` | 57 | `Help` |
| 23 | `Eye` | 58 | `AppBox` |
| 24 | `EyeOff` | 59 | `AppStore` |
| 25 | `Home` | 60 | `Calendar` |
| 26 | `Search` | 61 | `Game` |
| 27 | `Settings` | 62 | `Clock` |
| 28 | `Trash` | 63 | `Calculator` |
| 29 | `ArrowUp` | 64 | `LanguageHiragana` |
| 30 | `ArrowLeft` | 65 | `Browser` |
| 31 | `ArrowDown` | 66 | `Edit` |
| 32 | `ArrowRight` | 67 | `File` |
| 33 | `X` | 68 | `Folder` |
| 34 | `CheckboxOn` | 69 | `Copy` |

| 値 | 名前 | 値 | 名前 |
|---|---|---|---|
| 70 | `Pencil` | 75 | `Bucket` |
| 71 | `Line` | 76 | `Palette` |
| 72 | `Circle` | 77 | `Undo` |
| 73 | `SquareFilled` | 78 | `FolderOpen` |
| 74 | `CircleFilled` | 79 | `FilePlus` |

`35`(`CheckboxOff`)はtablerの`square`(白抜きの四角)なので、四角形の道具のアイコンにも使えます。

### text_align(Label / Textbox)

| 値 | 意味 |
|---|---|
| `0` | Left(既定) |
| `1` | Center |
| `2` | Right |

### direction(LayoutContainer)

| 値 | 意味 |
|---|---|
| `0` | VERTICAL(縦積み) |
| `1` | HORIZONTAL(横並び) |

### cross_align(LayoutContainer) / h_align・v_align(GridContainer)

| 値 | 意味 |
|---|---|
| `0` | START |
| `1` | CENTER |
| `2` | END |

### canvas_mode(CanvasRaster)

| 値 | 意味 |
|---|---|
| `0` | Line |
| `1` | Rect |
| `2` | Ellipse |
| `3` | Arrow |

### 色パレット(0〜15、PICO-8風)

| 値 | 色 | 値 | 色 |
|---|---|---|---|
| 0 | 黒(既定の前景色) | 8 | ダークグレー |
| 1 | ネイビー | 9 | 青 |
| 2 | ダークグリーン | 10 | 緑 |
| 3 | ダークシアン | 11 | シアン |
| 4 | マルーン | 12 | 赤 |
| 5 | パープル | 13 | マゼンタ |
| 6 | オリーブ | 14 | 黄 |
| 7 | ライトグレー | 15 | 白(既定の背景色) |

## HTTPメソッド・プロトコルの制約

- 対応メソッド: `GET` / `POST` / `PUT` / `PATCH` / `DELETE`
- `https://` も使えます。信頼するルート証明書は本体に焼き込まれたもの(Google / Let's Encrypt / DigiCert / Sectigo)と、SDの `/sys/tls/ca.pem` です。時計が合う(NTP同期)前は繋がりません。
- 自動リダイレクト追跡は `GET` のみです。
