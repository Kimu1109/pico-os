# lib/monocypher について

[Monocypher](https://monocypher.org/) 4.0.2(タグ `4.0.2`)の `src/monocypher.{c,h}` と
`src/optional/monocypher-ed25519.{c,h}` を、無改造でそのまま `src/` へ置いたもの。
ライセンスは BSD-2-Clause か CC0 の選択(`LICENCE.md`)。

SSHクライアント(`src/ssh/`)が使う:

| SSHでの用途 | 関数 |
|---|---|
| 鍵交換 curve25519-sha256 | `crypto_x25519()` / `crypto_x25519_public_key()` |
| ホスト鍵・公開鍵認証 ssh-ed25519 | `crypto_ed25519_check()` / `crypto_ed25519_sign()`(中でSHA-512) |
| 暗号 chacha20-poly1305@openssh.com | `crypto_chacha20_djb()`(64bitのnonce)/ `crypto_poly1305()` |

SHA-256(鍵交換のハッシュと鍵の導出)はMonocypherに無いので `src/ssh/Ssh_Sha256.hpp` に自前で持つ。

## なぜこれにしたか

- 実機(arduino-pico同梱のBearSSL)にはEd25519が無く、PC(OpenSSL)とは呼び方も違う。
  PC/Web/実機で**同じコードがそのまま動く**Cのライブラリが要った(`src/`は実機と同一、という方針)
- 動的確保をしない・Cのファイル2本・依存無し。組み込み向けに作られている
- Luaと同じく `lib/<名前>/src/` の形なので、PlatformIOは自動で拾い、`pc/CMakeLists.txt` も同じコピーを見る

## 版を上げる場合

1. https://github.com/LoupVaillant/Monocypher のタグを取得
2. 上の4ファイルを置き換え、`library.json` の `version` を更新
3. `sh script/host_test/run.sh`(ssh_crypto_test)と `sh script/host_test/run_net.sh`(ssh_net_test)を回す
