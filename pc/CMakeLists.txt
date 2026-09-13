# pico-os を PC 上で動かすためのビルド。
#
#   cmake -S pc -B pc/build && cmake --build pc/build -j
#   ./pc/build/picoos_pc
#
# 実機のライブラリ(Arduino / SdFat / SPI / XPT2046 / WiFi)は pc/compat/ の
# 代替ヘッダで置き換える。src/ 側には一切手を入れず、インクルードパスの
# 優先順位だけで差し替えている(script/host_test/stubs と同じ考え方)。
cmake_minimum_required(VERSION 3.16)
project(picoos_pc CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- LovyanGFX ----
# 既定ではここで取得する。手元にある物を使う場合は
#   cmake -S pc -B pc/build -DLOVYANGFX_DIR=/path/to/LovyanGFX
set(LOVYANGFX_DIR "" CACHE PATH "既存のLovyanGFXソースのパス(空なら自動で取得する)")

if(LOVYANGFX_DIR STREQUAL "")
    include(FetchContent)
    FetchContent_Declare(lovyangfx
        GIT_REPOSITORY https://github.com/lovyan03/LovyanGFX.git
        # platformio.ini の ^1.2.26 が解決する版に合わせる。
        # 実機側を上げたらここも追随させること
        GIT_TAG        1.2.28
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
find_package(SDL2 REQUIRED)

# ---- ソース ----
set(PICOOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

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

add_executable(picoos_pc
    "${CMAKE_CURRENT_SOURCE_DIR}/main_pc.cpp"
    ${PICOOS_SOURCES}
    ${LGFX_SOURCES})

# LGFX_SDL … LovyanGFX側がSDLパネルを有効にするためのフラグ
# PICOOS_PC  … pico-os側がPC向けの実装(パネル設定/タッチ取得)へ切り替えるためのフラグ
target_compile_definitions(picoos_pc PRIVATE
    LGFX_SDL
    PICOOS_PC
    # SDカードとして読むディレクトリ。実行時は PICOOS_SD_ROOT で差し替えられる
    PICOOS_SD_ROOT_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/sdcard")

# pc/compat を src より前に置くことで、実機ライブラリのヘッダを差し替える
target_include_directories(picoos_pc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${PICOOS_ROOT}/src"
    "${LOVYANGFX_DIR}/src"
    ${SDL2_INCLUDE_DIRS})

target_link_libraries(picoos_pc PRIVATE ${SDL2_LIBRARIES} pthread)
