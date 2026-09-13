# script/custom_icons

`generate_icons.py` が読む**自作アイコン**のSVG置き場。
tablerのアイコンは `script/tabler_icons/` にある。

## ここへ置く基準

tablerに手頃な絵が無いとき、または **tablerの絵柄が16pxで破綻するとき**。

tablerのアイコンは24pxグリッド・`stroke-width="2"`・丸キャップで描かれている。
これを16pxへ落とすと線の位置が非整数になり、閾値二値化(`BINARY_THRESHOLD=96`)で
太さがバラついたり、細い要素が丸ごと消えたりする。

実例として、電波強度アイコンは元々tablerの `wifi-0/wifi-1/wifi-2/wifi` を使っていたが:

- `wifi-0` は「点1つ・弧0本」(tablerでは**圏外**用)で、**16pxでは0ピクセル**になり完全に消えていた
- `wifi-1` も6ピクセルしか残らず、lv1とlv2が見分けられなかった
- そもそもtablerのwifiは弧が1/2/3本の3種類しか無いので、**4段階を作れない**

そのため `signal-bars-1〜4` を自作して差し替えた。

## 自作するときの決めごと

- **`viewBox` は `0 0 16 16`** にして、座標を整数に揃える。
  ステータスバーが16px表示なので、そこで最も綺麗に出ることを優先する。
  32/48/64pxへは整数倍拡大になるため、どのサイズでも崩れない。
- **`stroke` ではなく `fill` の矩形/パス**で描く。線幅の丸めでピクセルがズレないため。
- 角丸(`rx`)は入れない。16pxでは結果が変わらず、24pxでは細い要素が潰れる。
- 段階を表すアイコンは、**非アクティブな要素も残して「N個中何個目か」を読めるように**する
  (`signal-bars` は非アクティブな棒を足元の2x2ドットにしている)。
- 隣り合う要素の高さ差は**2px以上**空ける。1pxだと同じ大きさに見える
  (`signal-bars` の棒の最小値が3ではなく4なのはこのため)。
- SVGの先頭にコメントで**寸法と設計意図**を書き残す。後から追随しやすくなる。

## 収録アイコン

| ファイル | C++側のenum | 用途 |
|---|---|---|
| `signal-bars-1.svg` 〜 `signal-bars-4.svg` | `IconID::WifiSignal1` 〜 `WifiSignal4` | 電波強度。棒 x=1,5,9,13 / 幅2 / 高さ4,7,10,13 |

enum名が `WifiSignal*` のままなのは、意味(Wi-Fiの電波強度)が変わっておらず、
絵柄だけを差し替えたため。

## 圏外は専用アイコンを持たない

**組み合わせで表す**方針にしてある。`NetworkFunctions::GetWifiStateIconID()` は圏外でも
`WifiSignal1`(最弱)を返し、`Statusbar::render()` がその上へ `IconID::X` を `PICO_RED` で
重ねる。SDカードが `SdCard` + `X` で「SD無し」を表しているのと同じ組み立て方。

```cpp
IconRender::DrawIcon(NetworkFunctions::GetWifiStateIconID(), IconSize::Px16, x, y, PICO_BLACK);
if(!NetworkFunctions::IsConnected()){
    IconRender::DrawIcon(IconID::X, IconSize::Px16, x, y, PICO_RED);
}
```

以前は `signal-bars-off.svg`(足元ドット + 斜線)を持っていたが、斜線が細く
他のアイコンから浮いていたため削除した。**状態の否定はバツの重ね描きで統一する。**
