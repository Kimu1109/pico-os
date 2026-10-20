# PC/Webビルドの詳細

> `CLAUDE.md`から分けた設計記録(2026-10-20)。何がどこにあるかの地図は`CLAUDE.md`、進み具合は`SUMMARY.md`。
> パスはリポジトリのルートから。「上の「〜」」等の参照先が別のファイルにあるときは、`CLAUDE.md`の「設計記録の索引」で節の名前から探すこと。

## Webビルド (WebAssembly)

**ネイティブとの差はループの回し方(`main_pc.cpp`)とビルド設定だけ**で、`src/` も `compat/` も
共有している(`compat/` 内の分岐は `__EMSCRIPTEN__` で3か所のみ)。

- **ループ**: ブラウザのメインスレッドは止められないので、`Panel_sdl::main()`(別スレッドで
  ユーザコードを回す)は使えない。`Panel_sdl::setup()/loop()/close()` が公開されているので、
  `emscripten_set_main_loop()` から「`loop()` 1回 + `Panel_sdl::loop()` 1回」を刻む。
  **実測60fps**(pico-os側のループ負荷は120フレームで数ms)。
- **SDカード**: `pc/sdcard/` を `--preload-file` で `index.data` へ焼き込み、仮想FS(MEMFS)の
  `/sdcard` に載せる。POSIXのまま読めるので `compat/SdFat.h` は無改造。
  **中身を変えたら再ビルドが必要**で、書き込みはリロードで消える。
- **設定**: ブラウザに環境変数が無いので、`main_pc.cpp` が起動時にURLのクエリを `setenv()` する
  (`?wifi=disconnected&rssi=-85`)。`compat/` 側はいつもどおり `getenv` を読むだけ。
- **描画はソフトウェア(canvas 2D)に固定**。`main_pc.cpp`が起動時に
  `SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software")`を立てる(SDLはヒントで名指しした
  ドライバをACCELERATEDの要求と突き合わせないので、**LovyanGFXは無改造でよい**)。
  理由は速度ではなく**GPU描画だと「フレームは進んでいるのに画面が出ない」状態になりうる**こと:
  LovyanGFXの`sdl_create()`が`ACCELERATED|PRESENTVSYNC`を要求する→SDLのGLES2レンダラが
  `SDL_WINDOW_OPENGL`を足すため**`SDL_RecreateWindow()`でウィンドウを作り直す**→
  emscriptenのSDLはウィンドウを壊すとき**canvasを0x0へ縮める**
  (canvasそのものは壊せないため)。つまり**GPU描画は起動のたびにcanvasが0x0を通り**、
  作り直しに失敗すると0x0のまま残る。**WebGLの有無では判別できない**
  (「WebGLあり・canvas 0x0」の実報告あり)。ソフトウェア描画は`SDL_WINDOW_OPENGL`を
  要求しないので作り直しごと起きない。比較用に`?render=gl`でGPU描画に戻せる。
  速度はどちらも60fps。起動直後にcanvasの大きさをC++側で1回確認し、0x0なら
  `SDL_GetError()`ごとログへ出す。
- **canvasの大きさの判定をまたぐ**。emscriptenのSDLは`Emscripten_CreateWindow()`で
  canvasを1x1にしてCSS上の実測値を測り、**`floor(実測値) != 1`ならその実測値を画面の
  大きさに採用する**(external_size)。CSS無指定でもズームや端数で`0.9999998`が返ることがあり、
  `floor`で0→**canvasもウィンドウも0x0**→ソフトウェア描画が`createImageData(0,0)`で例外→
  **1フレーム目でループが止まる**(実報告あり)。そこで`main_pc.cpp`が**ウィンドウを作る間だけ
  canvasを1.5pxに固定**し(`pinCanvasCssSizeForProbe()`)、判定が必ず1になるようにしてから
  `releaseCanvasCssPin()`で外す。レイアウト前(実測値0)の間はウィンドウを作らせず待つ(最大60フレーム)。
  **external_size側へ倒してはいけない** — 480x640と正しく明示してもモードは同じで、
  SDLがウィンドウの内部サイズとCSSの箱を同期しなくなり、LovyanGFXの拡大率とタッチ換算の
  前提が崩れて**縦横比が崩れタップ位置がずれる**(これも実報告あり)。
  `pc/web/shell.html`側はcanvasに`max-width`等を普通に書いてよい(固定が外れた後に効くだけで、
  SDLがマウス座標をCSS上の大きさで割り戻すためタップはずれない)。詳細は`pc/README.md`。
- **Wi-Fiの疎通判定**: ソケットが無いので `navigator.onLine` を見る(`compat/WiFi.h`)。
- **Markdownブラウザのオンライン機能はWebでは動かない**。`compat/WiFiClient_PC.h` は生のTCP
  ソケットを使うが、ブラウザにはそれが無い(emscriptenはWebSocket経由へ流すので中継サーバが要る)。
  コンパイルは通り、SDから読む分(`/tmp/doc.md`)は普通に動くが、`browser-home` を設定したり
  「更新」「検索」を押してもサーバへは繋がらない。**Web公開版で試せるのはローカル文書まで。**
  HTTPSも同じ理由で持たない(`compat/WiFiClientSecure.h`が常に失敗するスタブになる)ので、
  カレンダーの取得もWebでは動かない(SDに焼き込んだ`.ics`の表示はできる)。
- **`delay()`**: 待つとタブが固まるのでWebでは即座に戻る(`src/` は使っていない)。
- **ページの外枠**: `pc/web/shell.html`(emscriptenの `--shell-file`)。canvas・ログ欄・
  Wi-Fi状態の切替・画面のPNG保存ボタンを持つ。デバッグ用の道具を足すならここ。
- **`src/`へ新しい依存を足すときはWebビルドも通すこと**。ネイティブが通ってもemscriptenで
  落ちる依存(生ソケット/スレッド/ブロッキング待ち)があるため。
- **公開**: `.github/workflows/web-pages.yml` が `main` へのpushで
  https://kimu1109.github.io/pico-os/ へ自動デプロイする(プルリクではビルド確認のみ)。
  **リポジトリの Settings > Pages で Source を「GitHub Actions」にしておくことが前提**
  (ワークフローからの自動有効化は`GITHUB_TOKEN`の権限ではできない)。
  emsdkの版はワークフローの `EMSDK_VERSION` で固定。公開中のコミットはページのログ先頭の
  `[WEB] pico-os build: <hash>` で分かる。
  **公開版の `index.js`/`index.wasm`/`index.data` はリビジョン付きの名前(`index.<hash>.js` 等)で置く**(2026-10-08)。
  同じ名前だとGitHub Pagesの`max-age=600`の間にブラウザが古い`index.js`と新しい`index.data`を組み合わせ、
  SDのファイルの位置がずれて別のファイルの途中を読んだ(公開直後に「ブロック」の`palette.lua:1: unexpected symbol near '<\227>'`として出た)。
  `shell.html`の`<meta name="picoos-asset-rev">`をワークフローが書き換え、`Module.locateFile`がそれを見て名前を差し替える
  (ローカルのビルドは書き換えないので従来の名前のまま)。
- **Lua APIリファレンスも同じPagesに置く**(2026-10-20): `lua-api-doc/`(Hugo)をワークフローがビルドし、
  https://kimu1109.github.io/pico-os/lua-api/ に出す。Pagesは1リポジトリ1サイトなので別ワークフローにはせず、
  `_site/lua-api/` として同じ成果物へ入れる(アプリストアの配布物と同じ扱い)。
  Hugoの版はワークフローの `HUGO_VERSION` で固定。`hugo.toml` の `baseURL` は手元確認用の `"/"` のままで、
  公開ビルドだけ `--baseURL` で上書きする(リンクは `relURL`/`RelPermalink` なので崩れない)。
  プルリクでもビルドして文書の壊れを止める。Webビルドの外枠(`shell.html`)に「Lua API リファレンス」へのリンクを置いた
  (手元の `pc/build-web` には `lua-api/` が無いのでリンク切れになる)。
  手元で見るなら `hugo server --source lua-api-doc`。
