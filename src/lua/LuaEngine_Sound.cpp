// pico.* のうち音(効果音・曲(MML)・WAV)
#include "lua/LuaEngine.hpp"
#include "lua/LuaEngine_Internal.hpp"
#include "services/Power_Functions.hpp"
#include "system/Mem_Functions.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/WidgetRegistry.hpp"
#include "gui/widgets/WidgetFactory.hpp"
#include "gui/widgets/WidgetProperty.hpp"
#include "gui/widgets/LayoutContainer.hpp"
#include "gui/widgets/GridContainer.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/LuaCanvas.hpp"
#include "gui/widgets/CanvasRaster.hpp"
#include "gui/widgets/Checkbox.hpp"
#include "gui/widgets/NumberSlider.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/TabBar.hpp"
#include "gui/widgets/DropdownMenu.hpp"
#include "gui/widgets/dialogs/MsgDialog.hpp"
#include "gui/widgets/dialogs/InputDialog.hpp"
#include "gui/widgets/dialogs/FileSaveDialog.hpp"
#include "gui/widgets/dialogs/FileSelectDialog.hpp"
#include "gui/widgets/dialogs/ColorDialog.hpp"
#include "gui/widgets/dialogs/PickerDialog.hpp"
#include "gui/icons/icon_render.h"
#include "gui/icons/Bmp_Codec.hpp"
#include "gui/icons/Jpeg_Codec.hpp"
#include "gui/Fill4bpp.hpp"
#include "gui/core/Widget_Functions.hpp"
#include "gui/core/Error_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "gui/core/Scene_Functions.hpp"
#include "services/App_Functions.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/core/Scene.hpp"
#include "gui/scenes/LuaScene.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "system/Config_Functions.hpp"
#include "net/Http_Request.hpp"
#include "util/Url.hpp"
#include "system/Time_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Pad_Functions.hpp"
#include "hal/Battery_Functions.hpp"
#include "system/Notification_Functions.hpp"
#include "sound/Note_Name.hpp"
#include "sound/Mml_Compiler.hpp"
#include "lua/LuaDebugger.hpp"
#include "lua/LuaJson.hpp"
#include "lua/LuaBuiltinModules.hpp"
#include "crypto/Secret_Aead.hpp"
#include "devtools/Profiler_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"
#include <Arduino.h>


using namespace LuaInternal;

int LuaEngine::l_sound_available(lua_State* L) {
    // アンプが刺さっていて、かつsound.cfgでoffにされていないとき(=実際に音が出るとき)だけtrue。
    // falseでもpico.beep()等は呼んでよい(黙って鳴ったことになる)。音で知らせる代わりに
    // 画面でも知らせたいアプリが見分けるためのもの
    lua_pushboolean(L, SoundFunctions::IsAvailable());
    return 1;
}

int LuaEngine::l_sound_rate(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)SoundFunctions::SampleRate());
    return 1;
}

int LuaEngine::l_sound_set_rate(lua_State* L) {
    // pico.sound_set_rate(44100) / (22050) / (nil)。このアプリが開いている間だけ効く(閉じる・別の画面へ移ると設定へ戻る)。
    // 変わるとそのとき鳴っている音は止まるので、音を鳴らす前(setup()等)に呼ぶ
    uint32_t rate = 0;
    if(!lua_isnoneornil(L, 1)){
        const lua_Integer v = luaL_checkinteger(L, 1);
        if(v != 0 && v != (lua_Integer)SoundFunctions::kSampleRateLow && v != (lua_Integer)SoundFunctions::kSampleRateHigh){
            return luaL_error(L, "pico.sound_set_rate: %d か %d か nil です",
                              (int)SoundFunctions::kSampleRateLow, (int)SoundFunctions::kSampleRateHigh);
        }
        rate = (uint32_t)v;
    }
    Self(L)->requested_rate_ = rate;
    //loop()は次のフレームで LuaScene::preferredSampleRate() を渡すが、この後すぐ鳴らす音を止めないよう今ここで切り替える
    lua_pushboolean(L, SoundFunctions::RequestSampleRate(rate));
    return 1;
}

int LuaEngine::l_beep(lua_State* L) {
    // チャンネル1で矩形波を鳴らすだけの簡易版(pico.sound_playの省略形)。
    // 長さは10秒で頭打ち(うっかり長い値を渡しても困らないように)
    const lua_Integer freq = luaL_checkinteger(L, 1);
    const lua_Integer ms   = luaL_checkinteger(L, 2);
    const uint16_t f = (uint16_t)std::clamp<lua_Integer>(freq, 0, 20000);
    const uint16_t d = (uint16_t)std::clamp<lua_Integer>(ms, 0, 10000);
    Self(L)->used_sound_ = true;
    SoundFunctions::Beep(f, d);
    return 0;
}

namespace {
    // pico.sound_play の wave に書ける名前(ChipSynth::Waveの並びと同じ順)
    const char* const kWaveNames[] = {
        "pulse12", "pulse25", "pulse50", "pulse75", "triangle", "saw", "noise", "noise_short",
    };
    static_assert(sizeof(kWaveNames) / sizeof(kWaveNames[0]) == (size_t)ChipSynth::Wave::kCount,
                  "kWaveNamesをChipSynth::Waveと揃えること");

    // Luaのチャンネル番号(1始まり)→ 0始まり。範囲外はエラー
    uint8_t CheckChannel(lua_State* L, int arg){
        const lua_Integer ch = luaL_checkinteger(L, arg);
        if (ch < 1 || ch > SoundFunctions::kChannels) {
            luaL_error(L, "チャンネルは1〜%dです(%d)", SoundFunctions::kChannels, (int)ch);
        }
        return (uint8_t)(ch - 1);
    }
}

int LuaEngine::l_sound_play(lua_State* L) {
    // pico.sound_play(ch, freq, ms [, {wave=, volume=, envelope=, pan=}]) -> bool
    const uint8_t ch = CheckChannel(L, 1);
    const lua_Number freq = luaL_checknumber(L, 2);
    const lua_Integer ms = luaL_checkinteger(L, 3);

    ChipSynth::Note note;
    note.freq_x16 = (freq <= 0) ? 0 : (uint32_t)std::min<lua_Number>(freq * 16.0 + 0.5, 1e9);
    //長さは1分で頭打ち。0は「止めるまで鳴らし続ける」
    note.length_ms = (uint32_t)std::clamp<lua_Integer>(ms, 0, 60000);

    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);

        lua_getfield(L, 4, "wave");
        if (!lua_isnil(L, -1)) {
            const char* name = luaL_checkstring(L, -1);
            bool found = false;
            for (size_t i = 0; i < (size_t)ChipSynth::Wave::kCount; i++) {
                if (strcmp(name, kWaveNames[i]) == 0) {
                    note.wave = (ChipSynth::Wave)i;
                    found = true;
                    break;
                }
            }
            if (!found) return luaL_error(L, "pico.sound_play: 不明な波形です(%s)", name);
        }
        lua_pop(L, 1);

        lua_getfield(L, 4, "volume");
        if (!lua_isnil(L, -1)) note.volume = (uint8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), 0, 15);
        lua_pop(L, 1);

        lua_getfield(L, 4, "envelope");
        if (!lua_isnil(L, -1)) note.envelope = (int8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), -7, 7);
        lua_pop(L, 1);

        lua_getfield(L, 4, "pan");
        if (!lua_isnil(L, -1)) note.pan = (int8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), -ChipSynth::kPanMax, ChipSynth::kPanMax);
        lua_pop(L, 1);
    }

    //周波数0は「止める」と同じ扱い(休符を書きやすいように)
    Self(L)->used_sound_ = true;
    if (note.freq_x16 == 0) {
        SoundFunctions::Stop(ch);
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, SoundFunctions::Play(ch, note));
    return 1;
}

int LuaEngine::l_sound_stop(lua_State* L) {
    // pico.sound_stop([ch]) chを省略すると全部
    if (lua_isnoneornil(L, 1)) SoundFunctions::StopAll();
    else SoundFunctions::Stop(CheckChannel(L, 1));
    return 0;
}

int LuaEngine::l_sound_playing(lua_State* L) {
    // pico.sound_playing([ch]) -> bool。chを省略するとどれか1つでも
    if (lua_isnoneornil(L, 1)) {
        lua_pushboolean(L, SoundFunctions::IsPlaying());
    } else {
        const uint8_t ch = CheckChannel(L, 1);
        lua_pushboolean(L, (SoundFunctions::ActiveChannels() >> ch) & 1);
    }
    return 1;
}

namespace {
    // 曲の読み込み結果をLuaへ返す: 成功なら true、失敗なら nil, "3行12列: 理由"
    int PushMusicResult(lua_State* L, bool ok, const MmlResult& r) {
        if (ok) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushnil(L);
        if (r.line > 0) lua_pushfstring(L, "%d行%d列: %s", r.line, r.col, r.message.c_str());
        else lua_pushstring(L, r.message.c_str());
        return 2;
    }
}

int LuaEngine::l_music_play(lua_State* L) {
    // pico.music_play(path) -> true | nil, 理由
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    MmlResult r;
    if (!OSData::SD_usable) {
        r.message.assign("SDカードが使えません");
        return PushMusicResult(L, false, r);
    }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.music_play: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        r.message.assign("このアプリからは読めない場所です");
        return PushMusicResult(L, false, r);
    }
    self->used_music_ = true;
    const bool ok = SoundFunctions::MusicPlayFile(path, &r);
    return PushMusicResult(L, ok, r);
}

int LuaEngine::l_music_play_text(lua_State* L) {
    // pico.music_play_text(mml) -> true | nil, 理由
    size_t len = 0;
    const char* text = luaL_checklstring(L, 1, &len);
    Self(L)->used_music_ = true;
    MmlResult r;
    const bool ok = SoundFunctions::MusicPlayText(text, len, &r);
    return PushMusicResult(L, ok, r);
}

int LuaEngine::l_music_stop(lua_State* L) {
    (void)L;
    SoundFunctions::MusicStop();
    return 0;
}

int LuaEngine::l_music_playing(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::MusicPlaying());
    return 1;
}

int LuaEngine::l_wav_play(lua_State* L) {
    // pico.wav_play(path[, {loop=bool, volume=0〜100}]) -> true | nil, 理由
    LuaEngine* self = Self(L);
    const char* path = luaL_checkstring(L, 1);
    bool loop = false;
    uint8_t volume = 100;
    //引数の誤りは状態(SD無し・権限)より先にエラーにする
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        lua_getfield(L, 2, "loop");
        if (!lua_isnil(L, -1)) {
            luaL_checktype(L, -1, LUA_TBOOLEAN);
            loop = lua_toboolean(L, -1);
        }
        lua_pop(L, 1);
        lua_getfield(L, 2, "volume");
        if (!lua_isnil(L, -1)) volume = (uint8_t)std::clamp<lua_Integer>(luaL_checkinteger(L, -1), 0, 100);
        lua_pop(L, 1);
    }

    if (!OSData::SD_usable) {
        lua_pushnil(L);
        lua_pushstring(L, "SDカードが使えません");
        return 2;
    }
    if (!self->SdPathAllowed(path)) {
        LOG_APP_WARN("pico.wav_play: アプリディレクトリ外へのアクセスは許可されていません: %s", path);
        lua_pushnil(L);
        lua_pushstring(L, "このアプリからは読めない場所です");
        return 2;
    }
    self->used_wav_ = true;
    const char* err = "";
    if (!SoundFunctions::WavPlay(path, loop, volume, &err)) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int LuaEngine::l_wav_stop(lua_State* L) {
    (void)L;
    SoundFunctions::WavStop();
    return 0;
}

int LuaEngine::l_wav_playing(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::WavPlaying());
    return 1;
}

int LuaEngine::l_note_freq(lua_State* L) {
    // pico.note_freq("C4" | 60) -> number | nil
    int note = -1;
    if (lua_type(L, 1) == LUA_TNUMBER) {
        if (!lua_isinteger(L, 1)) return luaL_error(L, "pico.note_freq: ノート番号は整数です");
        note = (int)std::clamp<lua_Integer>(lua_tointeger(L, 1), -1, 128);
    } else {
        note = NoteName::Parse(luaL_checkstring(L, 1));
    }
    const float f = NoteName::MidiToFreq(note);
    if (f <= 0.0f) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, f);
    return 1;
}

// ---- WAVの一時停止・位置・シーク(SoundFunctionsにあったものをLuaへ出しただけ) ----

int LuaEngine::l_wav_pause(lua_State* L) {
    // pico.wav_pause([paused=true]) -> 鳴らしているWAVがあればtrue
    const bool pause = lua_isnoneornil(L, 1) ? true : (lua_toboolean(L, 1) != 0);
    lua_pushboolean(L, SoundFunctions::WavPause(pause));
    return 1;
}

int LuaEngine::l_wav_paused(lua_State* L) {
    lua_pushboolean(L, SoundFunctions::WavPaused());
    return 1;
}

int LuaEngine::l_wav_position(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)SoundFunctions::WavPositionMs());
    return 1;
}

int LuaEngine::l_wav_duration(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)SoundFunctions::WavDurationMs());
    return 1;
}

int LuaEngine::l_wav_seek(lua_State* L) {
    const lua_Integer ms = luaL_checkinteger(L, 1);
    lua_pushboolean(L, ms >= 0 && SoundFunctions::WavSeekMs((uint32_t)ms));
    return 1;
}
