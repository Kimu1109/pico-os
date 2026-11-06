# script/ — 開発補助

誰が・いつ使うかでフォルダを分けてある(docs/design/devtools.md「script/ の置き場所」)。
よく使うものは `cli-tool/picoos` から名前で呼べる(`picoos --help`)。どのスクリプトも作業ディレクトリに頼らない。

| フォルダ | 中身 | 誰が・いつ |
|---|---|---|
| `pio/` | `pio_firmware.py`・`pio_firmware_targets.py`・`pio_hot_o2.py` | PlatformIO が実機ビルドの途中で読む(`platformio.ini` の `extra_scripts`) |
| `codegen/` | `src/` に入る生成物を作るもの: `generate_icons.py`(+材料の `tabler_icons/`・`custom_icons/`)・`generate_tls_roots.py`・`generate_jis_table.py`・`gen_file_share_page.py`・`gen_lua_builtins.py`・`i18n.py`・`font/` | 材料を変えたときに人が回す。一部は CI が `--check` で古くないかを見る |
| `check/` | `check_layers.py`・`check_variants.py`・`check_generated.py`(生成物が材料と合っているか。`picoos check`) | CI と、変更のたびに自分で |
| `release/` | `firmware_tool.py`・`build_fw_index.py`・`build_app_repo.py`・`web_shots.sh` | 配る(ファームの更新ファイル・Webの版の一覧・アプリストア・Webの画面写真) |
| `tools/` | `generate_pimg.py`・`pimg2png.py`・`generate_pvid.py`・`midi2mml.py`・`ppm2png.py`・`reference_server.py`・`pad_serial.py`・`spotify_auth.py` | 人(アプリの作者を含む)がデータを変換する・開発中に横で動かす |
| `assets/` | 同梱のLuaアプリの絵を作るもの | `picoos gen <名前>` |
| `dict/` | 辞書の材料とSD用のファイルを作るもの | `picoos dict build` |
| `host_test/` | ホストテスト(`run.sh`・`run_net.sh`・`run_mem.sh`) | CI と、変更のたびに自分で |
| `lua_test/` | Luaアプリのテスト | `picoos lua-test` |

- 新しいスクリプトは上の表のどれかに入れる(直下に置かない)。迷ったら「誰が・いつ動かすか」で選ぶ。
- `codegen/` の生成器には `--check`(作り直したものが今のファイルと同じかだけ見る)を付け、`check/check_generated.py` の一覧へ1行足す。
- 互いを import するものは同じフォルダに置く(`release/` の `firmware_tool.py` → `build_app_repo.py`、`tools/` の `pimg2png.py` → `generate_pimg.py`)。
- Python の依存は `requirements.txt`(`script/.venv` があれば `picoos` がそれを使う)。
