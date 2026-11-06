# docs/ — 文書の置き場所

文書は「誰が・何のために読むか」で置き場所を分ける(2026-11-06に整理)。新しい文書もこの表に合わせて置く。

| 置き場所 | 中身 | 読む人 |
|---|---|---|
| `spec/` | **外とやり取りする形式・取り決め**。相手(サーバ・別のリポジトリ・道具・アプリの作者)と合わせるもので、変えるときは互換性を考える | 相手の側を作る人 |
| `guide/` | **人が手を動かす手順**と、その結果の記録 | 利用者・実機を触る人 |
| `design/` | **機能ごとの設計の背景・経緯・調査**(決めたこと・やめたこと・確かめたこと)。索引は `CLAUDE.md`「設計記録の索引」 | 開発する人(Claude Code を含む) |
| `images/` | README 等に載せる画像 | |

## 今ある文書

| 文書 | 中身 |
|---|---|
| `spec/PROTOCOL.md` | ドキュメントサーバのプロトコル(Markdownブラウザが文書を取る取り決め。参照実装は `script/tools/reference_server.py`) |
| `spec/CHAT_PROTOCOL.md` | チャットのプロトコル(本体と `server/chat/`) |
| `spec/APP_REPO.md` | アプリストアの配布物の形式(本体と `kimu1109/pico-os-apps`・`script/release/build_app_repo.py`) |
| `spec/MUSIC_FORMAT.md` | 曲データ(pico-os MML)の形式 |
| `spec/VIDEO_FORMAT.md` | 動画(.pvid)の形式 |
| `guide/FIRMWARE_UPDATE.md` | ファームの更新のしかた(Web / SD / Wi-Fi)と更新ファイルの形 |
| `guide/HARDWARE_CHECKLIST.md` | 実機での確認・計測の手順と結果。**未完了かどうかは Todoist が正**(`CLAUDE.md`「人にしかできない確認」) |
| `design/*.md` | 機能ごとの設計記録(一覧は `CLAUDE.md`「設計記録の索引」)。大きくなったものはフォルダに分け、目次を `README.md` に置く(`sound/`・`lua-api/`)。節の名前は `grep -rn '^#.*名前' docs/design/` で引く |

## docs/ の外にある文書

- `CLAUDE.md`: 開発の地図と決まり(毎回読まれるので短く保つ)。`SUMMARY.md`: 機能の一覧と進み具合(項目名だけ)。
- `lua-api-doc/`: Luaアプリの作者向けの説明(別のサイトとして公開)。
- 各フォルダの `README.md`(`pc/`・`script/`・`site/`・`sdcard/` 等): そのフォルダの使い方。設計の背景は `design/` へ書き、ここには書かない。
- `lib/*/README-pico-os.md`: 取り込んだライブラリの版と手を入れたところ。
