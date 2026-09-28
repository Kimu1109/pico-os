# LovyanGFXへのパッチ

`pc/CMakeLists.txt`がFetchContentで取得したLovyanGFXのソースへ`PATCH_COMMAND`で
自動適用するパッチ。取得したソースへの初回populate時にだけ適用され、
`pc/build/_deps/lovyangfx-src`が既に存在する場合は再適用されない
(`platformio.ini`のLovyanGFXの版を上げてタグが変わったときは、
`_deps`ごと消してconfigureし直すことで新しい取得に対して再適用される)。

## 0001-panel_sdl-renderer-fallback-and-cleanup.patch

対象: `src/lgfx/v1/platforms/sdl/Panel_sdl.cpp` の `Panel_sdl::sdl_create()`

**直したバグ**: `Panel_sdl::sdl_update()`は`monitor.renderer == nullptr`の間
`sdl_create()`を毎フレーム呼び直す。本家の`sdl_create()`は
`SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)`
の戻り値をチェックしないため、この組み合わせのレンダラーを作れない環境
(**`SDL_VIDEODRIVER=dummy`が該当。CIやこのようなヘッドレス環境での標準的な
確認手順で使われる**)では`monitor.renderer`が永遠にnullptrのままになり、
`sdl_create()`が毎フレーム呼ばれ続ける。しかも呼ばれるたびに`SDL_CreateWindow()`で
新しいウィンドウを作り、直前の`m->window`を`SDL_DestroyWindow()`せずに
上書きするため、**毎フレーム1つ古いSDL_Windowがリークし続ける**。

pico-os側の実測(valgrind massif、`SDL_VIDEODRIVER=dummy`で400フレーム):
パッチ適用前は約2.2MB(400フレームで線形に増加、1フレームあたり約1.3〜1.4KB)、
適用後はヒープ下限が数KB程度の横ばいに収まることを確認済み
(CLAUDE.mdの「⚠ 未解決: PCビルドでシーン遷移を繰り返すとヒープ下限が
際限なく増える」の原因はこれだった)。

**パッチの内容**:
1. `sdl_create()`の先頭で、既存の`window`/`renderer`/`texture`/`texture_frameimage`を
   (在れば)必ず`SDL_Destroy*`してから作り直す。呼び直しが起きても蓄積しないための
   後始末で、初回呼び出し(何も無い状態)では無害。
2. `SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC`でのレンダラー生成に
   失敗したら、`SDL_RENDERER_SOFTWARE`を明示的に要求してフォールバックする
   (`SDL_VIDEODRIVER=dummy`はsoftwareレンダラー自体は提供できるが、`index=-1`の
   自動選択では先に列挙されるopengl/opengles2の失敗で終わってしまうため、
   明示的な指定が要る)。
3. レンダラーが1つも作れなかった場合に、以降の`SDL_CreateTexture`/
   `_update_scaling()`(`SDL_GetRendererOutputSize()`の出力先`rw`/`rh`が
   未初期化のまま除算に使われクラッシュしうる)を安全にスキップする。

## 再生成する場合

LovyanGFXの版(`platformio.ini`の`lib_deps`)を上げて`Panel_sdl.cpp`の該当箇所が
変わり、このパッチが当たらなくなったら、`Panel_sdl::sdl_create()`を上記の
3点に沿って手で直し、新旧の差分から`git diff --no-index`等でパッチを作り直すこと。
