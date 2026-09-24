# pico-os を PC / ブラウザ上で動かすためのビルド。
#
#   ネイティブ(SDL2のウィンドウ):
#     cmake -S pc -B pc/build && cmake --build pc/build -j
#     ./pc/build/picoos_pc
#
#   Web(WebAssembly):
#     emcmake cmake -S pc -B pc/build-web && cmake --build pc/build-web -j
#     emrun --no_browser --port 8080 pc/build-web   # → http://localhost:8080/index.html
#
# 実機のライブラリ(Arduino / SdFat / SPI / XPT2046 / WiFi)は pc/compat/ の
# 代替ヘッダで置き換える。src/ 側には一切手を入れず、インクルードパスの
# 優先順位だけで差し替えている(script/host_test/stubs と同じ考え方)。
cmake_minimum_required(VERSION 3.16)
project(picoos_pc CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PICOOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---- LovyanGFX ----
# 既定ではここで取得する。手元にある物を使う場合は
#   cmake -S pc -B pc/build -DLOVYANGFX_DIR=/path/to/LovyanGFX
set(LOVYANGFX_DIR "" CACHE PATH "既存のLovyanGFXソースのパス(空なら自動で取得する)")
# 取得するタグ。空なら platformio.ini の lib_deps から読む
set(LOVYANGFX_TAG "" CACHE STRING "取得するLovyanGFXのタグ(空ならplatformio.iniから読む)")

# 版は platformio.ini を唯一の情報源とする。
# 以前はここへ GIT_TAG を直書きしていたため、実機側を上げてもPCビルドが古い版の
# ままになりうる二重管理だった(追随は人間の記憶頼み)。
if(LOVYANGFX_DIR STREQUAL "" AND LOVYANGFX_TAG STREQUAL "")
    set(PICOOS_PIO_INI "${PICOOS_ROOT}/platformio.ini")
    if(NOT EXISTS "${PICOOS_PIO_INI}")
        message(FATAL_ERROR "platformio.ini が見つかりません: ${PICOOS_PIO_INI}")
    endif()

    # platformio.ini を書き換えたらconfigureをやり直させる(版の変更を拾うため)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PICOOS_PIO_INI}")

    file(READ "${PICOOS_PIO_INI}" PICOOS_PIO_INI_TEXT)
    if(NOT PICOOS_PIO_INI_TEXT MATCHES "lovyan03/LovyanGFX[ \t]*@[ \t]*([^\r\n \t;]+)")
        message(FATAL_ERROR
            "platformio.ini の lib_deps から LovyanGFX の版を読めませんでした。\n"
            "'lovyan03/LovyanGFX@1.2.28' の形式で書くか、"
            "-DLOVYANGFX_TAG=<タグ> で明示してください。")
    endif()
    set(LOVYANGFX_TAG "${CMAKE_MATCH_1}")

    # ^1.2.26 や ~1.2.26 のような範囲指定だと、PlatformIOが実際に落としてくる版が
    # 確定しない。PC側だけ別の版になっては同期する意味が無いので、ここで止めて
    # 完全固定を促す(どうしても範囲指定のままにしたい場合は -DLOVYANGFX_TAG=)
    if(NOT LOVYANGFX_TAG MATCHES "^[0-9]")
        message(FATAL_ERROR
            "platformio.ini の LovyanGFX が範囲指定(${LOVYANGFX_TAG})になっています。\n"
            "実機とPCで違う版になりうるため、'lovyan03/LovyanGFX@1.2.28' のように"
            "完全固定してください。\n"
            "範囲指定のままにする場合は -DLOVYANGFX_TAG=1.2.28 のように明示してください。")
    endif()

    message(STATUS "LovyanGFX: platformio.ini の指定は ${LOVYANGFX_TAG}")
endif()

if(LOVYANGFX_DIR STREQUAL "")
    include(FetchContent)
    FetchContent_Declare(lovyangfx
        GIT_REPOSITORY https://github.com/lovyan03/LovyanGFX.git
        GIT_TAG        ${LOVYANGFX_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(lovyangfx)
    if(NOT lovyangfx_POPULATED)
        # add_subdirectory はしない(LovyanGFX同梱のCMakeListsはESP-IDF向けのため)
        FetchContent_Populate(lovyangfx)
    endif()
    set(LOVYANGFX_DIR "${lovyangfx_SOURCE_DIR}")
endif()

message(STATUS "LovyanGFX: ${LOVYANGFX_DIR}")

# ---- SDL2 ----
# Emscriptenでは -sUSE_SDL=2 でportsのSDL2が使われるので find_package はしない
if(NOT EMSCRIPTEN)
    find_package(SDL2 REQUIRED)
    # HTTPS(pc/compat/WiFiClientSecure_PC.h)。実機はarduino-pico同梱のBearSSLを使う。
    # Webビルドは生のソケットが無いので、そもそもTLSも持たない(compat側でスタブになる)
    find_package(OpenSSL REQUIRED)
endif()

# ---- ソース ----

file(GLOB_RECURSE PICOOS_SOURCES CONFIGURE_DEPENDS "${PICOOS_ROOT}/src/*.cpp")

# PC向けに必要なLovyanGFXのソース(examples_for_PC/CMake_SDL と同じ並び)
file(GLOB LGFX_SOURCES CONFIGURE_DEPENDS
    "${LOVYANGFX_DIR}/src/lgfx/Fonts/efont/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/Fonts/IPA/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/Fonts/lvgl/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/utility/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/v1/*.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/lv_font/*.c"
    "${LOVYANGFX_DIR}/src/lgfx/v1/misc/*.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_Device.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/panel/Panel_FrameBufferBase.cpp"
    "${LOVYANGFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp")

# ---- Lua ----
# git/tarballをFetchContentで取得せず、lib/lua/にvendorした本体をそのままビルドする。
# 理由はlib/lua/README-pico-os.md参照(要約: Lua本体のsrc/にはmain()を持つ
# lua.c/luac.cが混在しており、PlatformIOの自動収集にそのまま渡すと実機ビルドで
# Arduinoコア自身のmain()と衝突する。vendor時点で2ファイルを除いてあるので、
# PlatformIO(lib/配下の自動収集)とこのCMakeの両方が常に同じソースを見る)。
#
# LUA_USE_LINUX等は定義しない。RP2350実機はdlopen等のPOSIX動的ロードを持たないため、
# PC/WebビルドもLuaの既定(ANSI)構成に揃えて実機と同じ経路にする
# (dlopenが要る`require`でのCモジュール読み込みは元々使わない)。
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${PICOOS_ROOT}/lib/lua/src/*.c")

add_executable(picoos_pc
    "${CMAKE_CURRENT_SOURCE_DIR}/main_pc.cpp"
    ${PICOOS_SOURCES}
    ${LGFX_SOURCES}
    ${LUA_SOURCES})

# LGFX_SDL … LovyanGFX側がSDLパネルを有効にするためのフラグ
# PICOOS_PC  … pico-os側がPC向けの実装(パネル設定/タッチ取得)へ切り替えるためのフラグ
target_compile_definitions(picoos_pc PRIVATE LGFX_SDL PICOOS_PC)

# pc/compat を src より前に置くことで、実機ライブラリのヘッダを差し替える
target_include_directories(picoos_pc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${PICOOS_ROOT}/src"
    "${PICOOS_ROOT}/lib/lua/src"
    # Peanut-GB(ヘッダ1本。実装の取り込みはsrc/gb/Gb_Emu.cppだけ。lib/peanut_gb/README-pico-os.md)
    "${PICOOS_ROOT}/lib/peanut_gb/src"
    "${LOVYANGFX_DIR}/src")

# LuaのCソースをC++コード([-fpermissive]無しで]警告無く混在ビルドできるよう、
# 拡張子で明示的に「Cとしてコンパイルする」設定を入れる
# (CMAKE_CXX_STANDARDがCソースへ波及しないよう、LANGUAGE Cを明示する)
set_source_files_properties(${LUA_SOURCES} PROPERTIES LANGUAGE C)

if(EMSCRIPTEN)
    # ---- Web(WebAssembly)ビルド ----
    # SDカードは pc/sdcard/ を丸ごとパッケージしてブラウザの仮想FS(MEMFS)へ載せる。
    # 実行時に差し替えられないので、ネイティブと違ってパスは固定
    target_compile_definitions(picoos_pc PRIVATE PICOOS_SD_ROOT_DEFAULT="/sdcard")

    # SDL2はemscriptenのportsを使う(コンパイル時にもヘッダのパスとして必要)
    target_compile_options(picoos_pc PRIVATE "-sUSE_SDL=2")

    # どのコミットのビルドが公開されているかをページのログで分かるようにする。
    # CI(.github/workflows/web-pages.yml)がコミットハッシュを渡す
    set(PICOOS_WEB_REV "dev" CACHE STRING "Webビルドに刻むリビジョン表記")
    target_compile_definitions(picoos_pc PRIVATE PICOOS_WEB_REV="${PICOOS_WEB_REV}")

    # index.html / index.js / index.wasm / index.data を吐く。
    # ディレクトリをそのまま静的配信すればトップページで動く
    set_target_properties(picoos_pc PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")

    target_link_options(picoos_pc PRIVATE
        "-sUSE_SDL=2"
        # ヒープはシーン遷移で伸び縮みするので成長を許可する
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=64MB"
        # ウィジェットツリーの再帰(visitAll)が深くなるので既定の64KiBから広げる
        "-sSTACK_SIZE=4MB"
        # emscripten_set_main_loop で回すので main() を抜けてもランタイムは残す
        "-sEXIT_RUNTIME=0"
        # クエリ文字列の取り出し(main_pc.cpp の EM_ASM)で使う
        "-sEXPORTED_RUNTIME_METHODS=stringToUTF8,UTF8ToString"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/sdcard@/sdcard"
        "--shell-file" "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")

    # シェルとSDの中身を変えたらリンクし直す
    set_property(TARGET picoos_pc APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
else()
    # ---- ネイティブビルド ----
    # SDカードとして読むディレクトリ。実行時は PICOOS_SD_ROOT で差し替えられる
    target_compile_definitions(picoos_pc PRIVATE
        PICOOS_SD_ROOT_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/sdcard")

    target_include_directories(picoos_pc PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(picoos_pc PRIVATE ${SDL2_LIBRARIES} OpenSSL::SSL OpenSSL::Crypto pthread)
endif()
