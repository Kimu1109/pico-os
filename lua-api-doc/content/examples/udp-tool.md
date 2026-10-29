---
title: "UDPツール"
weight: 47
description: "/lua/apps/UDPツール/ — pico.socket.udp で機器と送り受けし、Wake on LAN でPCを起こす"
---

UDPで機器へ文字を送り、届いたものを並べるアプリです。登録したPCを Wake on LAN で起こすこともできます(`app.cfg` と `icon.pimg` 付き)。[`pico.socket.udp`](../../api/data/socket/) の使い方のまとまった例になっています。

## 画面

```
[戻る][設定][送信][起動]
宛先 192.168.1.50:4210
待ち受け: 返事だけ受ける
← {"led":"on","rssi":-52}        ← 届いたもの(緑)。宛先以外からなら送り元も付く
→ status                         ← 送ったもの(青)
← [107B] 65 63 68 6F 3A FF …     ← 文字として読めないものは長さと16進
```

- **送信**: 入れた文字を宛先へ1つのデータグラムで送ります(1472バイトまで)。
- **起動**: 登録したPCを選ぶと、マジックパケット(`FF`×6 + MACアドレス×16 = 102バイト)を送ります。
- **設定**: 宛先(`ホスト:ポート`)・待ち受けの番号(0 なら空いている番号で返事だけ受ける)・WoLの宛先・WoLの機器(1行に `名前=MACアドレス`)・記録を消す。

WoLの宛先は既定でブロードキャスト `255.255.255.255:9` です。届かないときは、`192.168.1.255:9` のようなLANのブロードキャストアドレスや、PCのIPを直接書いてください。

## 作りのポイント

```lua
sock = pico.socket.udp(function(ev, data, host, port)
    if ev == "data" then add("← " .. host .. ":" .. port .. "  " .. show(data)) end
end, cfg.local_port > 0 and { port = cfg.local_port } or nil)

pico.socket.sendto(sock, "192.168.1.50", 4210, "status")

-- Wake on LAN
local mac = "\0\17\34\51\68\85"              -- 00:11:22:33:44:55
pico.socket.sendto(sock, "255.255.255.255", 9, string.rep("\255", 6) .. string.rep(mac, 16))
```

- UDPは繋ぐ手順が無いので、作ったらすぐ送れます(`"open"` は来ません)。
- 返事は、送ったときと同じソケットに `"data"` で届きます。待ち受けの番号を決めなくても、返事は受けられます。
- 待ち受けの番号を替えるときは、古いソケットを `pico.socket.close()` してから作り直します。
