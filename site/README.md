# site/ — GitHub Pages のページ

https://kimu1109.github.io/pico-os/ に載るページのうち、PC/Webビルドの外にあるもの。
`.github/workflows/web-pages.yml` が `_site/` へ写して公開する(docs/design/pc-web.md)。

| ファイル | 公開先 | 中身 |
|---|---|---|
| `landing.html` | `/` | 入口のページ(Playground・ダウンロード・Lua APIへのリンクと画面写真) |
| `download.html` | `/download/` | ファームのダウンロード(`/firmware/downloads.json` を読む。作るのは `script/release/build_fw_index.py`) |
| `store.html` | (pico-os-apps のサイト) | アプリストアの一覧の雛形。`script/release/build_app_repo.py site` が読む。投稿用リポジトリは `web/` へ写して使う |

- Playground(Web版の本体、`/playground/`)の外枠は `pc/web/shell.html`(Emscripten のビルドが使うので PC/Webビルドの側に置く)。
- ページの中のリンクは公開後のURLを基準にしている(`download/`・`../` 等)。このフォルダの中の並びとは関係ない。
- トップの画面写真は CI が撮る(`script/release/web_shots.sh`)。手元で見るときは `site/shots/` へ撮る(.gitignore 済み)。
- 見た目はどのページも同じパレット・線・フォントに揃えてある(`shell.html`・`lua-api-doc/static/css/style.css` も)。
