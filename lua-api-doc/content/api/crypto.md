---
title: "暗号化"
weight: 85
description: "encrypt / decrypt / is_encrypted / hash / random_bytes と、store_save の暗号化"
---

Luaアプリがデータを暗号化して持つための関数です。暗号は XChaCha20-Poly1305(改ざんも検出する認証付き暗号)、パスワードから鍵を作るのは Argon2id です。**何から何を守れるかは鍵の決め方で変わる**ので、下の「守れる範囲」を必ず読んでください。

## pico.encrypt

<div class="sig">pico.encrypt(plain: string, password?: string) <span class="ret">-> text: string | nil [, reason: string]</span></div>

`plain`(バイナリ可、12KiBまで)を暗号化して、`"enc2:"` で始まる文字列を返します。そのままファイル(`pico.sd_write`)やJSONに入れられます。毎回ランダムなnonceを使うので、同じ平文でも暗号文は毎回変わります。

失敗(大きすぎる・メモリ不足)は `nil, 理由` です。`password` が文字列でなければエラー。

## pico.decrypt

<div class="sig">pico.decrypt(text: string, password?: string) <span class="ret">-> plain: string | nil [, reason: string]</span></div>

`pico.encrypt` の結果を元に戻します。次の場合は `nil, 理由` を返します(例外にはなりません)。

- パスワードが違う、または別のアプリの暗号文(パスワード無しの場合)
- 暗号文が改ざん・破損している
- パスワード付きの暗号文なのに `password` が無い
- 形式が違う(`enc2:` で始まらない、base64が壊れている)

パスワード無しの暗号文に `password` を渡しても、そのまま読めます。

## pico.is_encrypted

<div class="sig">pico.is_encrypted(text: string) <span class="ret">-> boolean</span></div>

`"enc2:"` で始まるかを見るだけです(中身が正しいかは見ません)。

## pico.hash

<div class="sig">pico.hash(data: string, key?: string) <span class="ret">-> hex: string</span></div>

BLAKE2b-256 を64桁の16進で返します。`key`(64バイトまで)を渡すと鍵付きのハッシュ(メッセージ認証コード)になります。パスワードをそのまま保存する代わりのハッシュには向きません(総当たりに弱いため。パスワードの検証は `encrypt`/`decrypt` の成否で行ってください)。

## pico.random_bytes

<div class="sig">pico.random_bytes(n: integer) <span class="ret">-> data: string</span></div>

暗号に使える乱数を `n` バイト(1〜1024)返します。実機はRP2350のハードウェア乱数です。

## 暗号化して保存する

[`pico.store_save`](../scenes/) / `pico.store_load` に `{ encrypt = true }` か `{ password = "..." }` を渡すと、アプリの保存データ(`store.json`)を暗号化できます。ほかのファイルは、`pico.sd_write(path, pico.encrypt(data, password))` のように自分で組み合わせてください。

## 鍵の決め方と、守れる範囲

| 方法 | 鍵の元 | 守れるもの | 守れないもの |
|---|---|---|---|
| パスワードを渡す | パスワード + ランダムなsalt(Argon2id・メモリ64KiB・3パス) | SDを盗まれても、本体を分解されてファームを吸い出されても、**パスワードが強ければ**読めない | パスワードを知っている/推測できる相手。GPUで総当たりする相手(メモリ64KiBはデスクトップ向けの設定より弱い)。**長いパスフレーズを使ってください** |
| パスワードを渡さない | ファームに焼かれた固定鍵 + アプリのフォルダ名 | **SDカードだけ**を盗まれた/見られた場合。別のアプリがあなたのデータを復号すること | ファームを吸い出せる相手。ソースを見て固定鍵(`Secret_Cipher.hpp` の `kKey`)を知っている相手。OSをforkして使うなら `kKey` を書き換えてください |

どちらの方法でも、**動いている最中のアプリのメモリを覗かれる**こと(デバッガ、クラッシュダンプ)は防げません。また、暗号文を丸ごと別の時点のものに差し替える「巻き戻し」は検出できません(改ざんは検出しますが、古い正しい暗号文は正しいので)。

## 暗号文の形式

`"enc2:"` + base64url(ヘッダ + nonce(24バイト) + 暗号文 + MAC(16バイト))。ヘッダの先頭1バイトが版(1=パスワード無し、2=パスワード付き)で、パスワード付きはArgon2のパス数・ブロック数・salt(16バイト)が続きます。ヘッダも認証の対象なので、パラメータだけを書き換えても検出されます。PCから読みたい場合は、Monocypher の `crypto_aead_unlock`(XChaCha20-Poly1305)で同じ形式を復号できます。

## 使用例

```lua
-- パスワードを聞いて、設定を暗号化して保存する
local d = pico.show_input("パスワード", "", true)
pico.on(d, "closed", function(_, ok, pw)
    if not ok or pw == "" then return end
    if pico.store_save({ wifi = "secret" }, { password = pw }) then
        pico.toast("保存しました")
    end
end)

-- 読むとき
local data, why = pico.store_load({ password = pw })
if not data then pico.toast("読めません: " .. tostring(why)) end
```
