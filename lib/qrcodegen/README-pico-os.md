# lib/qrcodegen について

[QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library)(Project Nayuki)1.8.0
(タグ `v1.8.0`)の `c/qrcodegen.{c,h}` を、無改造でそのまま `src/` へ置いたもの。ライセンスはMIT(`LICENSE`)。

QRコードの表示部品 `QRCodeView`(`src/gui/widgets/QRCodeView.{hpp,cpp}`、Luaからは `pico.create("QRCode")`)が
`qrcodegen_encodeText()` を使う。数字だけ・英大文字と一部の記号だけの文字列は自動で詰めた形になり、
それ以外(日本語を含む)はUTF-8のバイト列のまま入る。

## なぜこれにしたか

- 動的確保をしない(呼び出し側が渡す2本のバッファだけで符号化する)・Cのファイル2本・依存無し
- 誤り訂正(リード・ソロモン)と8種のマスクの評価まで規格どおりに実装され、各言語版で広く使われている
- Luaやmonocypherと同じく `lib/<名前>/src/` の形なので、PlatformIOは自動で拾い、`pc/CMakeLists.txt` も同じコピーを見る

## 版を上げる場合

1. https://github.com/nayuki/QR-Code-generator のタグを取得
2. `c/qrcodegen.c` と `c/qrcodegen.h` を置き換え、`library.json` の `version` を更新
3. `sh script/host_test/run.sh widget`(qrcode_test)を回す
