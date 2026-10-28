# ホストテスト(PCのg++ + ASan/UBSanで実コードを動かす)のビルド定義。
#
#   sh script/host_test/run.sh [グループ名...]  … これを configure → build → ctest する入口
#
# 作り:
#   - src/ の .cpp を層(script/check_layers.py の段)ごとの静的ライブラリにする
#     (picoos_base / picoos_system / … / picoos_boot)。どのファイルがどの段かは
#     check_layers.py --sources が答えるので、ここにファイルの一覧は書かない。
#   - テストは全部の段のライブラリを1つのリンクの組(RESCAN)でリンクする。
#     静的ライブラリはリンカが「足りないシンボルを持つオブジェクト」だけを拾うので、
#     テストが自分で偽物を書いた関数の本物は引っ張られない(以前 run.sh で1本ずつ
#     並べていたリンクの一覧は、これで要らなくなった)。
#   - グループ(core / apps / …)は CTest のラベル。同じグループの中は1本ずつ
#     (RESOURCE_LOCK)、別のグループとは並列に回る(以前の run_parallel.sh と同じ)。
#
# テストを足すとき:
#   C++のテスト  … script/host_test/<名前>.cpp を書き、下の「テストの一覧」に
#                  host_test(<名前> GROUP <グループ>) を1行足す
#   Luaのテスト  … <名前>.lua を書き、host_lua_test(<名前> GROUP <グループ>)
#   src/ に新しい .cpp を足したとき … 何もしなくてよい(ホストのスタブで作れない
#                  ものだけ HOST_EXCLUDE に理由付きで足す)
cmake_minimum_required(VERSION 3.24)
project(picoos_host_test C CXX)
enable_testing()

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
cmake_path(NORMAL_PATH ROOT)
set(SRC ${ROOT}/src)

# グループの一覧(run.sh --list と CI の matrix がこれを見る。1行で書くこと)
set(HOST_TEST_GROUPS core apps media input calc widget lua-base lua-engine lua-ext lua-sandbox lua-scene ssh)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)   # gnu++17(以前の run.sh と同じ)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)     # gnu99

option(HOST_TEST_SANITIZE "ASan/UBSanを付ける" ON)
set(SAN_FLAGS -g)
if(HOST_TEST_SANITIZE)
    list(APPEND SAN_FLAGS -fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()
add_compile_options(${SAN_FLAGS})

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)   # PC版の WiFiClientSecure(net/Http_Transport が使う)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- vendorしたCのライブラリ ----
# pc/CMakeLists.txt と同じく LUA_USE_LINUX 等は定義しない(実機と同じANSI構成)
file(GLOB LUA_SOURCES ${ROOT}/lib/lua/src/*.c)
add_library(host_lua STATIC ${LUA_SOURCES})
target_include_directories(host_lua PUBLIC ${ROOT}/lib/lua/src)

add_library(host_monocypher STATIC
    ${ROOT}/lib/monocypher/src/monocypher.c
    ${ROOT}/lib/monocypher/src/monocypher-ed25519.c)
target_include_directories(host_monocypher PUBLIC ${ROOT}/lib/monocypher/src)

add_library(host_qrcodegen STATIC ${ROOT}/lib/qrcodegen/src/qrcodegen.c)
target_include_directories(host_qrcodegen PUBLIC ${ROOT}/lib/qrcodegen/src)

# JPEGの読み取り(pico.image_load)が使うTJpgDec(LovyanGFXの写し。stubs/lgfx/utility/README.md)
add_library(host_tjpgd STATIC ${CMAKE_CURRENT_SOURCE_DIR}/stubs/lgfx/utility/lgfx_tjpgd.c)
# PNGの読み取りが使う pngle と、その展開に使う miniz(どちらもLovyanGFXの写し。同じREADME)
add_library(host_pngle STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/lgfx/utility/lgfx_pngle.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs/lgfx/utility/lgfx_miniz.c)
# 写しには手を入れないので、中のUB(4バイト単位の読み・符号付きのシフト。実機のCPUでは問題にならない)はUBSanで見ない
target_compile_options(host_pngle PRIVATE -fno-sanitize=undefined)

# ---- src/ を層ごとのライブラリに ----
# ホストのスタブ(stubs/)では作れないもの。src/ にあってもテストには入れない
# (どれも今のテストでは使っていない。使うときはスタブを足してからここから外す)
set(HOST_EXCLUDE
    main.cpp                          # setup()/loop() はテストごとに要らない
    OS_Data.cpp                       # テストはスタブの OS_Data.hpp(stubs/)を使う
    system/Log_Functions.cpp          # スタブの FsFile に open() が無い(テストは LogFunctions を偽物にする)
    hal/Network_Functions.cpp         # スタブの WiFi に scanNetworks() が無い
    gui/core/GFX_Functions.cpp        # 液晶の設定(LGFX_Config_PC.hpp)が pc/compat にしか無い
    apps/ssh/ui/TerminalView.cpp      # スタブに fonts::AsciiFont8x16 が無い
    devtools/font_coverage_check.cpp  # スタブに lgfx::IFont が無い
    gui/widgets/systems/Statusbar.cpp # スタブの setCursor() にフォントを取る形が無い
    services/Display_Functions.cpp    # スタブの LGFX に sleep()/wakeup() が無い
    services/UsbDrive_Functions.cpp   # SD_Functions.hpp が SPI.h を読む(スタブに無い)
    gui/scenes/SettingsScene.cpp      # スタブの WiFi に scanNetworks() が無い(_Wifi/_Time/_Other も同じヘッダを読む)
    gui/scenes/SettingsScene_Wifi.cpp
    gui/scenes/SettingsScene_Time.cpp
    gui/scenes/SettingsScene_Other.cpp
    gui/scenes/TouchCalibScene.cpp    # 実機のタッチ(Touch_Functions.hpp)を読む
    lua/LuaDebugScreen.cpp            # 実機のタッチ(Touch_Functions.hpp)を読む
)

execute_process(
    COMMAND ${Python3_EXECUTABLE} ${ROOT}/script/check_layers.py --sources
    OUTPUT_VARIABLE LAYER_SOURCES
    RESULT_VARIABLE LAYER_RC)
if(NOT LAYER_RC EQUAL 0)
    message(FATAL_ERROR "script/check_layers.py --sources が失敗しました")
endif()
# src/ のファイルが増減したら configure をやり直す
file(GLOB_RECURSE _all_src CONFIGURE_DEPENDS ${SRC}/*.cpp ${SRC}/*.c)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ROOT}/script/check_layers.py)

set(HOST_LAYERS base system lib ui service app boot)
foreach(layer ${HOST_LAYERS})
    set(LAYER_FILES_${layer})
endforeach()
string(REPLACE "\n" ";" LAYER_LINES "${LAYER_SOURCES}")
foreach(line ${LAYER_LINES})
    if(line STREQUAL "")
        continue()
    endif()
    string(REPLACE " " ";" parts "${line}")
    list(GET parts 0 layer)
    list(GET parts 1 rel)
    if(rel IN_LIST HOST_EXCLUDE)
        continue()
    endif()
    list(APPEND LAYER_FILES_${layer} ${SRC}/${rel})
endforeach()

set(HOST_LAYER_LIBS)
foreach(layer ${HOST_LAYERS})
    if(NOT LAYER_FILES_${layer})
        continue()
    endif()
    add_library(picoos_${layer} STATIC ${LAYER_FILES_${layer}})
    target_compile_definitions(picoos_${layer} PUBLIC PICOOS_PC)
    # スタブを src/ より先に(実機の Arduino.h 等をスタブで差し替える)
    target_include_directories(picoos_${layer} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${SRC} ${ROOT}/lib/peanut_gb/src)
    target_link_libraries(picoos_${layer} PUBLIC host_lua host_monocypher host_qrcodegen)
    list(APPEND HOST_LAYER_LIBS picoos_${layer})
endforeach()

# テストがリンクするもの。段の間は互いに参照しあう(つなぎの boot が下の段の関数を定義する等)
# ので、RESCAN の組にしてリンカに何度でも見直させる
set(HOST_LINK
    "$<LINK_GROUP:RESCAN,${HOST_LAYER_LIBS},host_lua,host_monocypher,host_qrcodegen,host_tjpgd,host_pngle>"
    OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

# ---- テストの登録 ----
set(HOST_TEST_TIMEOUT 60)   # 1本あたり(以前の run.sh と同じ。無限ループで止まらないように)

function(_host_test_props test group)
    if(NOT group IN_LIST HOST_TEST_GROUPS)
        message(FATAL_ERROR "${test}: グループ ${group} は HOST_TEST_GROUPS にありません")
    endif()
    set_tests_properties(${test} PROPERTIES
        LABELS ${group}
        TIMEOUT ${HOST_TEST_TIMEOUT}
        RESOURCE_LOCK group_${group}
        WORKING_DIRECTORY ${ROOT})
    set_property(GLOBAL APPEND PROPERTY HOST_GROUP_TARGETS_${group} ${ARGN})
endfunction()

# 実行ファイルだけ作る(テストの下請け。mml_dump / pvid_dump / lua_script_test 等)
#   host_exe(<名前> [SOURCES 追加の.cpp...] [COMPAT])
#     COMPAT … stubs/ の代わりに pc/compat/ を先に見る(実時間の micros() が要る step_budget_test)
function(host_exe name)
    cmake_parse_arguments(A "COMPAT" "" "SOURCES" ${ARGN})
    add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp ${A_SOURCES})
    if(A_COMPAT)
        target_include_directories(${name} BEFORE PRIVATE ${ROOT}/pc/compat)
    endif()
    target_link_libraries(${name} PRIVATE ${HOST_LINK})
endfunction()

#   host_test(<名前> GROUP <グループ> [ARGS 引数...] [SOURCES ...] [COMPAT])
function(host_test name)
    cmake_parse_arguments(A "COMPAT" "GROUP" "ARGS;SOURCES" ${ARGN})
    set(extra)
    if(A_COMPAT)
        set(extra COMPAT)
    endif()
    host_exe(${name} SOURCES ${A_SOURCES} ${extra})
    add_test(NAME ${name} COMMAND ${name} ${A_ARGS})
    _host_test_props(${name} ${A_GROUP} ${name})
endfunction()

#   host_lua_test(<名前> GROUP <グループ>) … lua_script_test で <名前>.lua を動かす
function(host_lua_test name)
    cmake_parse_arguments(A "" "GROUP" "" ${ARGN})
    add_test(NAME ${name}
        COMMAND lua_script_test ${CMAKE_CURRENT_SOURCE_DIR}/${name}.lua ${ROOT})
    _host_test_props(${name} ${A_GROUP} lua_script_test)
endfunction()

#   host_py_test(<名前> GROUP <グループ> USES <実行ファイル...> ARGS 引数...)
function(host_py_test name)
    cmake_parse_arguments(A "" "GROUP" "USES;ARGS" ${ARGN})
    add_test(NAME ${name}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/${name}.py ${A_ARGS})
    _host_test_props(${name} ${A_GROUP} ${A_USES})
endfunction()

# ======================== テストの一覧 ========================
# 各テストの中身は script/host_test/<名前>.cpp の先頭のコメント

# --- core: 土台(シーン遷移・ラベル・設定・暗号・パス・HTTP・ファーム・アプリ登録簿) ---
host_test(scene_test GROUP core)
host_test(msc_bridge_test GROUP core)
host_test(label_test GROUP core)
host_test(markdown_test GROUP core)
host_test(config_test GROUP core)
host_test(secret_cipher_test GROUP core)
host_test(argon2_split_test GROUP core)
host_test(wifi_profiles_test GROUP core)
host_test(captive_portal_test GROUP core)
host_test(setup_portal_test GROUP core)
host_test(file_share_test GROUP core)
host_test(app_test GROUP core)
host_test(home_test GROUP core)
host_test(app_repo_test GROUP core)
host_exe(app_package_test)
host_py_test(app_package_tool_test GROUP core USES app_package_test
    ARGS $<TARGET_FILE:app_package_test> ${CMAKE_CURRENT_BINARY_DIR}/app_package_work)
host_py_test(registry_tool_test GROUP core ARGS ${CMAKE_CURRENT_BINARY_DIR}/registry_tool_work)
host_test(firmware_test GROUP core)
host_py_test(firmware_tool_test GROUP core USES firmware_test
    ARGS $<TARGET_FILE:firmware_test> ${CMAKE_CURRENT_BINARY_DIR}/firmware_tool_work)
host_test(bmp_test GROUP core)
host_test(image_codec_test GROUP core)
host_test(path_test GROUP core)
host_test(touch_filter_test GROUP core)
host_test(touch_calib_test GROUP core)
host_test(cache_test GROUP core)
host_test(file_ops_test GROUP core)
host_test(http_test GROUP core)
host_test(socket_test GROUP core)
host_test(discovery_test GROUP core)

# --- apps: カレンダー・ファイル検索・チャット・TODO・メール ---
host_test(ical_test GROUP apps)
host_test(calendar_scene_test GROUP apps)
host_test(file_search_test GROUP apps)
host_test(notes_test GROUP apps)
host_test(calendar_sync_secret_test GROUP apps)
host_test(chat_proto_test GROUP apps)
host_test(chat_scene_test GROUP apps)
host_test(todoist_proto_test GROUP apps)
host_test(xml_reader_test GROUP apps)
host_test(todo_test GROUP apps)
host_test(mail_proto_test GROUP apps)
host_test(reader_test GROUP apps)
host_test(mail_scene_test GROUP apps)

# --- media: ゲームボーイ・音・マイク・曲・動画 ---
host_test(gb_emu_test GROUP media)
host_test(gb_apu_test GROUP media)
host_test(sound_test GROUP media)
host_test(wav_test GROUP media)
host_test(music_test GROUP media)
host_test(mic_test GROUP media)
# ハードウェア機能を外した版(PICO_HW_*=0)の空の実装。このテストだけ外した定義で作り、空の実装を自分に入れる
host_test(hw_off_test GROUP media SOURCES
    ${SRC}/hal/Sound_Functions_Off.cpp ${SRC}/hal/Mic_Functions_Off.cpp ${SRC}/hal/Battery_Functions.cpp)
target_compile_definitions(hw_off_test PRIVATE PICO_HW_SOUND=0 PICO_HW_MIC=0 PICO_HW_BATTERY=0)
host_test(play_queue_test GROUP media)
host_exe(mml_dump)
host_py_test(midi2mml_test GROUP media USES mml_dump ARGS $<TARGET_FILE:mml_dump>)
host_exe(pvid_dump)
host_py_test(pvid_test GROUP media USES pvid_dump ARGS $<TARGET_FILE:pvid_dump>)

# --- input: コントローラー・キーボード・電源・ロック・端末の鍵 ---
host_test(pad_test GROUP input)
host_test(cardkb_test GROUP input)
host_test(cardkb_uart_test GROUP input)
host_test(gb_keymap_test GROUP input)
host_test(wii_classic_test GROUP input)
host_test(rtc_test GROUP input)
host_test(edit_history_test GROUP input)
host_test(romaji_kana_test GROUP input)
host_test(key_input_test GROUP input)
host_test(dialog_key_test GROUP input)
host_test(power_test GROUP input)
host_test(lock_test GROUP input)
host_test(devkey_test GROUP input)

# --- calc: 電卓・辞書 ---
host_test(calc_eval_test GROUP calc)
host_test(calculator_test GROUP calc)
host_test(calc_modes_test GROUP calc)
host_test(dict_test GROUP calc)
host_test(dict_scene_test GROUP calc)

# --- widget: ウィジェットの生成/プロパティ・フォーカス・アラーム・通知・エラー表示 ---
host_test(widget_factory_test GROUP widget)
host_test(focus_test GROUP widget)
host_test(widget_property_test GROUP widget)
host_test(qrcode_test GROUP widget)
host_test(step_budget_test GROUP widget COMPAT)
host_test(alarm_test GROUP widget)
host_test(notification_test GROUP widget)
host_test(error_functions_test GROUP widget)
host_test(widget_arena_test GROUP widget)

# --- lua-base: vendorしたLua本体 ---
host_test(lua_smoke_test GROUP lua-base)
host_test(lua_stdlib_test GROUP lua-base)
host_test(lua_alloc_budget_test GROUP lua-base)

# --- lua-engine / lua-ext / lua-sandbox: LuaEngine(pico.*)---
host_test(lua_engine_test GROUP lua-engine)
host_test(lua_ext_test GROUP lua-ext)
host_test(lua_socket_test GROUP lua-ext)
host_test(background_test GROUP lua-ext)
host_test(lua_sandbox_test GROUP lua-sandbox)
host_test(devtools_test GROUP lua-sandbox)

# --- lua-scene: LuaScene・アプリの走査・Luaアプリのゲームの規則・2.5Dエンジン ---
host_test(lua_scene_test GROUP lua-scene)
host_test(lua_app_scanner_test GROUP lua-scene)
host_exe(lua_script_test)
host_lua_test(tetris_test GROUP lua-scene)
host_lua_test(oshite_test GROUP lua-scene)
host_test(iso_world_test GROUP lua-scene ARGS ${ROOT})
host_test(iso_td_test GROUP lua-scene ARGS ${ROOT})
host_lua_test(reversi_test GROUP lua-scene)
host_lua_test(minesweeper_test GROUP lua-scene)
host_lua_test(breakout_test GROUP lua-scene)
host_lua_test(blocks_test GROUP lua-scene)
host_lua_test(spotify_test GROUP lua-scene)
host_lua_test(qrcode_app_test GROUP lua-scene)
host_lua_test(pico_mock_test GROUP lua-scene)
host_lua_test(pico_test_test GROUP lua-scene)

# --- ssh: 端末エミュレータと暗号まわりの小道具 ---
host_test(vt_terminal_test GROUP ssh)
host_test(ssh_util_test GROUP ssh)

# グループごとのビルド対象(run.sh が「そのグループのテストだけ作る」のに使う)
foreach(group ${HOST_TEST_GROUPS})
    get_property(targets GLOBAL PROPERTY HOST_GROUP_TARGETS_${group})
    list(REMOVE_DUPLICATES targets)
    add_custom_target(group_${group} DEPENDS ${targets})
endforeach()
