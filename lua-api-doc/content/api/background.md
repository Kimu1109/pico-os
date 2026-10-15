---
title: "バックグラウンド"
weight: 86
description: "app.cfg の background / is_background / finish"
---

アプリを閉じている間も、決まった間隔でスクリプトを動かせます(天気を取ってきて通知する、RSSの新着を数える、など)。

画面のLuaアプリ(予算200KB)と同時にもう1つフルのエンジンを持つほどのメモリは無いので、バックグラウンドは
**使い捨て**です。間隔ごとに小さな予算(48KB)の新しいエンジンを作って `background.lua` を上から1回動かし、
終わったら捨てます。回をまたいで覚えたいことは `pico.store_save` / `pico.settings_set` / `pico.sd_write` で残してください
(グローバル変数は次の回には残りません)。

## app.cfg

```ini
permission_background = true    # これが無いと動かない
background = background.lua     # アプリのフォルダからの相対パス
background_interval = 900       # 秒(既定900、10以上)
permission_notify = true        # 通知を出すなら
permission_network = true       # 通信するなら
```

- 動いているかどうか・最後の結果(失敗の理由)は、設定アプリの「他」→「バックグラウンド」[一覧] で見られます。
  ここで入り切りでき([切る]/[入れる]。`/sys/background.cfg` に覚えます)、[今すぐ] で待たずに1回動かせます
- 何かが入っている間は、ステータスバーに青い印(回る矢印)が出ます
- 起動してから最初の回は約30秒後(Wi-Fiの接続と時計合わせを待つため)。同時に動くのは1本だけです
- そのアプリを開いている間は動きません(同じファイルを書き合わないように。画面側のスクリプトに任せる)
- アプリストアの配布物では、権限の欄に `background` が要ります(`build_app_repo.py` が app.cfg から足します)

## 1回の流れ

1. `background.lua` を上から実行する
2. `pico.http_request` の応答待ちや `pico.after` / `pico.every` のタイマーが残っていれば、毎フレームそれを進める
3. 何も残っていない・`pico.finish()` が呼ばれた・**30秒**を過ぎた、のどれかで終わる(30秒を過ぎたら「時間切れ」)

```lua
-- background.lua: 1時間ごとに天気を取ってきて、雨なら知らせる
pico.http_request("GET", "https://example.com/weather.json", nil, nil, function(ok, status, body)
    if not ok or status ~= 200 then return end      -- 失敗しても次の回にまた試す
    local w = pico.json_decode(body)
    local last = pico.store_load() or {}
    if w and w.rain and not last.rain then
        pico.notify{ title = "天気", body = "雨が降りそうです", tag = "rain" }
    end
    pico.store_save{ rain = w and w.rain or false }
end)
```

## 使える pico.*

画面を持たないので、ウィジェット・描画・ダイアログ・画面の遷移・入力・音・`pico.iso` は使えません(`nil` です)。使えるのは:

- 通知: `notify` `notify_cancel` `notify_list`
- 通信: `http_request` `http_cancel` `http_close` `form_encode`
- SD・保存: `sd_*` `config_*` `store_load` `store_save` `settings_*` `app_dir` `path_join`
- データ: `json_*` `json_pick` `sd_json_pick` `xml_pick` `sd_xml_pick` `url_*` `base64_*`、暗号(`encrypt` 等)
- 時間: `get_time` `time` `millis` `micros` `after` `every` `cancel`
- その他: `log` `battery` `wifi_status` `memory_info` `require` `traceback`、下の2つ

権限(`permission_network` 等)は画面のときと同じように効きます。命令数の上限(1回の呼び出しで200万命令)も同じです。

エラーはダイアログを出しません(今前に出ている別のアプリの上に出ても困るため)。ログと一覧の「失敗: …」、
`/crash/lua_NNNN.txt` に残ります。

## pico.is_background

<div class="sig">pico.is_background() <span class="ret">-> boolean</span></div>

バックグラウンドで動いているなら `true`。画面と同じモジュールを `require` して使い分けるときに。

## pico.finish

<div class="sig">pico.finish() <span class="ret">-> boolean</span></div>

この回の仕事を終える(残っているタイマーや通信を待たない)。バックグラウンドでなければ何もせず `false`。

```lua
local id = pico.every(1000, function() ... end)
-- 何かの条件で
pico.finish()
```
