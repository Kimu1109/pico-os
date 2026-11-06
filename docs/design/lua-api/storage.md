# Lua API の設計記録(pico.*): SDカード・設定ファイル

> `CLAUDE.md`から分けた設計記録(2026-10-20)。2026-11-06に `lua-api.md` から節ごとに分けた(目次は同じフォルダの `README.md`)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`grep -rn '^#.*節の名前' docs/design/` で節の名前から探すこと。

## SDカードアクセス(2026-09-20実装)

`pico.sd_exists/read/write/remove/mkdir/list`は`SD_Functions`/`FileExplorer`/
`PICO_IO`が既に持っている操作をLuaへ薄く橋渡ししただけで、新しい判断はほぼ無い。

- **`OSData::SD_usable == false`の間はどれも例外にせず失敗値(`false`/`nil`)を返す**。
  SD無しは配線の状態であってスクリプトの書き方の誤りではないため、
  `pico.create`の未知種別のような「プログラマの誤り」枠(`luaL_error`)には入れなかった。
- **`pico.sd_read`には上限(`kMaxSdReadBytes` = 16KiB、`LuaScene::kMaxScriptBytes`と
  同じ値)がある。** スクリプト読み込み(`LuaScene`)は上限超過時に「切り詰めて使う」
  判断をしているが、任意のデータファイルを同じように黙って切り詰めると、
  スクリプトが壊れたJSON/セーブデータを気付かずに使ってしまう恐れがあるため、
  **`sd_read`は切り詰めずに`nil`を返して失敗させる**設計にした(スクリプト読み込みとは
  意図的に判断を変えた点)。
  読み込み自体は`MarkdownView::load()`/`LuaScene::loadAndRun()`と同じく、
  ファイル全体ぶんの一時バッファをヒープへ一度に確保せず、256Bのスタックチャンクで
  `luaL_Buffer`へ読み進める(Lua文字列自体はLua側の確保になるので、
  `LuaEngine`のメモリ予算(既定200KB)がそのまま上限としても効く)。
- **`sd_remove`はディレクトリと判明したら`PICO_IO::removeRecursive()`を使う**
  (`FileExplorer::on_press_delete()`と同じ判断)。
- **`sd_list`は`{name=..., is_dir=...}`の配列を返す**(`FileExplorer::update_list()`と
  同じ`openNext()`の走査。ファイル名バッファも同じ128B)。
  ディレクトリという概念を持たないホストテストのSdFatスタブでは空配列しか返らない
  ため、`lua_engine_test.cpp`では「クラッシュしないこと」までしか確認できておらず、
  実際の列挙結果はPCビルド(`pc/compat/SdFat.hは実ファイルシステム`)の`--shot`で
  確認した(`sd_write`→`sd_read`→`sd_exists`→追記→`sd_list`→`sd_remove`→
  再度`sd_exists`の一連が期待通りに動くことを確認済み)。
- **`sd_read`/`sd_write`等はFsFileを開いたままLuaのC API(`luaL_Buffer`/テーブル構築)を
  呼ぶ。** その最中にLua側のメモリ予算超過でエラー(`longjmp`。ANSI Cのsetjmp/longjmp
  ベースなのでC++デストラクタは呼ばれない)が起きると、開いたままの`FsFile`の
  `close()`が飛ばされ得る。ごく小さな読み書きの最中に限られる稀なエッジケースであり、
  「Lua着手前の受け皿の状態」表にある**OS内部90箇所のOOM未対応と同じ割り切りで
  対象外**とした(そこまで手を入れる投資対効果は低いと判断)。

## 設定ファイルとapp.cfgの書き込み制限(2026-09-30実装)

`pico.config_read/config_get/config_write`。セーブデータや設定を`pico.sd_read`+自前の文字列解析で扱っていたのを、
OSと同じ`key=value`書式(`Config_Functions`)で読み書きできるようにした。読みは`PICO_Config::ParseFile()`、
書きは`PICO_Config::SetValue()`(一時ファイル経由で1キーだけ差し替え)をそのまま使う。

- 値は常に**文字列**で返す(型の解釈はスクリプト側)。書くときは文字列/数値/真偽値を受け、`AsInt/AsFloat/AsBool`で
  読み戻せる表記にする(小数は`%.6f`から末尾の0を削る。`AsFloat()`が指数表記を拒むので`%g`は使えない)。
- **改行を含む値は`false`**(次の行として別のキー=権限等を差し込めるため)。不正なキー(`=`・改行・前後の空白・`#`始まり)は`luaL_error`。
- 読みは`pico.sd_read`と同じ16KiB上限。ファイルが無い/ディレクトリならnil。

**app.cfgの書き込み制限**: `app.cfg`は`permission_network`/`permission_sd_outside_app_dir`を持つので、
スクリプトが書き換えられると自分の権限を上げられてしまう(次回起動のスキャンで効く)。書き込み系の共通ガード
`LuaEngine::SdWriteAllowed()`(`sd_write`/`sd_remove`/`sd_mkdir`/`canvas_save`/`config_write`が通る)で、
権限に関わらず以下を拒否する(`false`+`LOG_APP_WARN`):

- 自分の`app_dir`直下の`app.cfg`、および`/lua/apps/<名前>/app.cfg`(どのアプリのものでも)。後者は
  `sd_outside_app_dir`を持つアプリが他のアプリの権限を上げる・新しいアプリを`app.cfg`付きで作るのを防ぐため。
- `sd_remove`では、それらを含むディレクトリ(自分の`app_dir`とその祖先、`/lua/apps/<名前>`とその祖先)も。
- パスは`normalize()`の後、**FATと同じく大小を区別せず、各セグメントの先頭の空白・末尾の空白と`.`を捨てて**比べる
  (`APP.CFG`や`app.cfg.`も同じファイルを指すため)。
- 読むのは自由(`sd_read`/`config_read`)。
- ホストテストは`lua_engine_test.cpp`(読み書きの往復・後勝ち・型ごとの書式・改行の拒否・各経路でのapp.cfg拒否・
  大文字/末尾の`.`/`..`・`sd_outside_app_dir=true`でも他アプリの`app.cfg`は拒否)。
