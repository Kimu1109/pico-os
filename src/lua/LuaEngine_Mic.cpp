// LuaEngine のマイク(pico.mic_*)。中身は MicFunctions(SPH0645、2コア目で受けて1コア目がWAVへ書く)。
//
// - 録音・メーター・増幅/周波数の変更には権限 microphone(app.cfg の permission_microphone)が要る。
//   周りの音を拾うので既定では許さない。権限が無ければ nil, 理由(pico.notify と同じ「実行時の状態」枠)
// - 引数の誤りは権限より先に luaL_error
// - 録音先は pico.sd_write と同じ SdWriteAllowed()(app_dir の外は sd_outside_app_dir が要る、app.cfg へは書けない)。
//   省略するとアプリのフォルダの rec_0001.wav から空いている番号
// - アプリを閉じると(LuaEngine の破棄)、このアプリが始めた録音とメーターを止め、変えた増幅・周波数を元へ戻す
//   (mic.cfg へは書かない)。録音の後片付け(残りを書いて閉じる)は MicFunctions::Update() が続ける
// - バックグラウンドのスクリプトでは使えない(kBackgroundApi に入れていない)

#include "lua/LuaEngine.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "hal/Mic_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "sound/Mic_Dsp.hpp"
#include "OS_Data.hpp"

namespace {
    LuaEngine* Self(lua_State* L){ return *static_cast<LuaEngine**>(lua_getextraspace(L)); }

    bool EndsWithWav(const char* p){
        const size_t n = strlen(p);
        return n >= 5 && strcasecmp(p + n - 4, ".wav") == 0;
    }

    const char* InputName(MicFunctions::Input in){
        switch(in){
            case MicFunctions::Input::Off:      return "off";
            case MicFunctions::Input::Starting: return "starting";
            case MicFunctions::Input::Running:  return "running";
            case MicFunctions::Input::Failed:   return "failed";
        }
        return "off";
    }

    lua_Integer CheckRate(lua_State* L, int idx, const char* api){
        const lua_Integer r = luaL_checkinteger(L, idx);
        if(!MicDsp::ValidRate((uint32_t)r)) return luaL_error(L, "%s: 周波数は 16000 / 24000 / 48000 です", api);
        return r;
    }
}

struct LuaEngineMic {
    static bool Denied(lua_State* L, LuaEngine* self, const char* api){
        if(self->permissions_.microphone) return false;
        LOG_APP_WARN("%s: マイクの権限がありません(app.cfgのpermission_microphone)", api);
        lua_pushnil(L);
        lua_pushstring(L, "マイクの権限がありません");
        return true;
    }

    // 変える前の値を1回だけ覚えてから変える(閉じるときに戻す)
    static void SetGain(LuaEngine* self, int db){
        if(self->mic_saved_gain_ < 0) self->mic_saved_gain_ = MicFunctions::GainDb();
        MicFunctions::SetGainDb(db, false);
    }
    static bool SetRate(LuaEngine* self, uint32_t rate){
        const uint32_t before = MicFunctions::SampleRate();
        if(!MicFunctions::SetSampleRate(rate, false)) return false;
        if(self->mic_saved_rate_ == 0 && rate != before) self->mic_saved_rate_ = before;
        return true;
    }

    static int l_mic_start(lua_State* L){
        // pico.mic_start([path][, {rate=, gain=}]) -> path | nil, 理由
        LuaEngine* self = Self(L);
        const char* path = nullptr;
        int opt = 1;
        if(lua_type(L, 1) == LUA_TSTRING){
            path = lua_tostring(L, 1);
            opt = 2;
            if(!EndsWithWav(path)) return luaL_error(L, "pico.mic_start: 録音先は .wav のファイルです");
        }else if(!lua_isnoneornil(L, 1) && lua_type(L, 1) != LUA_TTABLE){
            return luaL_argerror(L, 1, "録音先のパス(文字列)か設定の表です");
        }else if(lua_isnil(L, 1)){
            opt = 2;
        }
        lua_Integer rate = 0, gain = -1;
        if(!lua_isnoneornil(L, opt)){
            luaL_checktype(L, opt, LUA_TTABLE);
            lua_getfield(L, opt, "rate");
            if(!lua_isnil(L, -1)) rate = CheckRate(L, -1, "pico.mic_start");
            lua_pop(L, 1);
            lua_getfield(L, opt, "gain");
            if(!lua_isnil(L, -1)){
                gain = luaL_checkinteger(L, -1);
                if(gain < MicDsp::kMinGainDb || gain > MicDsp::kMaxGainDb){
                    return luaL_error(L, "pico.mic_start: gain は %d〜%d(dB)です", MicDsp::kMinGainDb, MicDsp::kMaxGainDb);
                }
            }
            lua_pop(L, 1);
        }

        if(Denied(L, self, "pico.mic_start")) return 2;
        if(MicFunctions::IsRecording()){
            lua_pushnil(L);
            lua_pushstring(L, MicFunctions::RecErrorToStr(MicFunctions::RecError::Busy));
            return 2;
        }
        if(!OSData::SD_usable){
            lua_pushnil(L);
            lua_pushstring(L, "SDカードが使えません");
            return 2;
        }

        char auto_path[PICO_PATH_LEN + 1];
        if(!path){
            //アプリのフォルダの rec_0001.wav から空いている番号
            const char* dir = self->app_dir_.c_str();
            const bool root = strcmp(dir, "/") == 0;
            bool found = false;
            for(int i = 1; i <= 9999 && !found; i++){
                snprintf(auto_path, sizeof(auto_path), "%s/rec_%04d.wav", root ? "" : dir, i);
                found = !OSData::SD.exists(auto_path);
            }
            if(!found){
                lua_pushnil(L);
                lua_pushstring(L, MicFunctions::RecErrorToStr(MicFunctions::RecError::NoName));
                return 2;
            }
            path = auto_path;
        }
        if(!self->SdWriteAllowed(path, "pico.mic_start")){
            lua_pushnil(L);
            lua_pushstring(L, "このアプリからは書けない場所です");
            return 2;
        }
        //置き場のフォルダが無ければ作る(SdWriteAllowed を通った場所の親なので、持ち場の中)
        char parent[PICO_PATH_LEN + 1];
        if(PICO_IO::parent(parent, path) && strlen(parent) > 1 && !OSData::SD.exists(parent)) OSData::SD.mkdir(parent);

        if(rate > 0) SetRate(self, (uint32_t)rate);
        if(gain >= 0) SetGain(self, (int)gain);
        const MicFunctions::RecError e = MicFunctions::StartRecording(path);
        if(e != MicFunctions::RecError::None){
            lua_pushnil(L);
            lua_pushstring(L, MicFunctions::RecErrorToStr(e));
            return 2;
        }
        self->mic_recording_ = true;
        lua_pushstring(L, MicFunctions::RecordingPath());
        return 1;
    }

    static int l_mic_stop(lua_State* L){
        // pico.mic_stop() -> このアプリの録音を止めたら true
        LuaEngine* self = Self(L);
        const bool mine = self->mic_recording_ && MicFunctions::IsRecording() && !MicFunctions::IsFinishing();
        if(mine) MicFunctions::StopRecording();
        self->mic_recording_ = false;
        lua_pushboolean(L, mine);
        return 1;
    }

    static int l_mic_recording(lua_State* L){
        // 録音中か(止めた後の書き終える間も真)
        lua_pushboolean(L, MicFunctions::IsRecording());
        return 1;
    }

    static int l_mic_info(lua_State* L){
        lua_createtable(L, 0, 12);
        lua_pushboolean(L, MicFunctions::IsRecording());           lua_setfield(L, -2, "recording");
        lua_pushboolean(L, MicFunctions::IsFinishing());           lua_setfield(L, -2, "finishing");
        lua_pushstring(L, MicFunctions::RecordingPath());          lua_setfield(L, -2, "path");
        lua_pushinteger(L, (lua_Integer)MicFunctions::RecordedMs());  lua_setfield(L, -2, "ms");
        lua_pushinteger(L, (lua_Integer)MicFunctions::DroppedSamples()); lua_setfield(L, -2, "dropped");
        lua_pushinteger(L, (lua_Integer)MicFunctions::Overruns()); lua_setfield(L, -2, "overruns");
        lua_pushinteger(L, (lua_Integer)MicFunctions::SampleRate()); lua_setfield(L, -2, "rate");
        lua_pushinteger(L, MicFunctions::GainDb());                lua_setfield(L, -2, "gain");
        lua_pushstring(L, InputName(MicFunctions::InputState()));  lua_setfield(L, -2, "input");
        lua_pushboolean(L, MicFunctions::HasSignal());             lua_setfield(L, -2, "signal");
        const MicFunctions::StopReason r = MicFunctions::LastStopReason();
        if(r != MicFunctions::StopReason::None){
            lua_pushstring(L, MicFunctions::StopReasonToStr(r));
            lua_setfield(L, -2, "stop_reason");
        }
        return 1;
    }

    static int l_mic_monitor(lua_State* L){
        // pico.mic_monitor(on) -> true | nil, 理由。録音せずに聞く(メーター用)
        LuaEngine* self = Self(L);
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        const bool on = lua_toboolean(L, 1);
        if(on && Denied(L, self, "pico.mic_monitor")) return 2;
        MicFunctions::SetMonitor(on);
        self->mic_monitor_ = on;
        lua_pushboolean(L, 1);
        return 1;
    }

    static int l_mic_level(lua_State* L){
        // pico.mic_level() -> 前に読んでからの一番大きい振幅(0〜1), その dBFS(無音は -96)
        const int32_t peak = MicFunctions::TakePeak();
        const double v = (double)peak / 32768.0;
        lua_pushnumber(L, (lua_Number)v);
        lua_pushnumber(L, (lua_Number)(peak > 0 ? 20.0 * log10(v) : -96.0));
        return 2;
    }

    static int l_mic_has_signal(lua_State* L){
        lua_pushboolean(L, MicFunctions::HasSignal());
        return 1;
    }

    static int l_mic_set_gain(lua_State* L){
        // pico.mic_set_gain(db) -> true | nil, 理由(0〜36へ丸める)
        LuaEngine* self = Self(L);
        const lua_Integer db = luaL_checkinteger(L, 1);
        if(Denied(L, self, "pico.mic_set_gain")) return 2;
        SetGain(self, (int)db);
        lua_pushboolean(L, 1);
        return 1;
    }

    static int l_mic_gain(lua_State* L){
        lua_pushinteger(L, MicFunctions::GainDb());
        return 1;
    }

    static int l_mic_set_rate(lua_State* L){
        // pico.mic_set_rate(16000|24000|48000) -> true | nil, 理由(録音中は変えられない)
        LuaEngine* self = Self(L);
        const lua_Integer rate = CheckRate(L, 1, "pico.mic_set_rate");
        if(Denied(L, self, "pico.mic_set_rate")) return 2;
        if(!SetRate(self, (uint32_t)rate)){
            lua_pushnil(L);
            lua_pushstring(L, "録音中は周波数を変えられません");
            return 2;
        }
        lua_pushboolean(L, 1);
        return 1;
    }

    static int l_mic_rate(lua_State* L){
        lua_pushinteger(L, (lua_Integer)MicFunctions::SampleRate());
        return 1;
    }
};

void LuaEngine::ReleaseMic(){
    if(mic_recording_ && MicFunctions::IsRecording() && !MicFunctions::IsFinishing()) MicFunctions::StopRecording();
    mic_recording_ = false;
    if(mic_monitor_) MicFunctions::SetMonitor(false);
    mic_monitor_ = false;
    if(mic_saved_gain_ >= 0) MicFunctions::SetGainDb(mic_saved_gain_, false);
    mic_saved_gain_ = -1;
    //録音の後片付けの間は周波数を変えられないので、終わってから戻す
    if(mic_saved_rate_ != 0) MicFunctions::RestoreSampleRate(mic_saved_rate_);
    mic_saved_rate_ = 0;
}

bool LuaEngine::MicBusy() const {
    return mic_recording_ && MicFunctions::IsRecording();
}

void LuaEngine::RegisterMicApi(){
    registerFn("mic_start", LuaEngineMic::l_mic_start);
    registerFn("mic_stop", LuaEngineMic::l_mic_stop);
    registerFn("mic_recording", LuaEngineMic::l_mic_recording);
    registerFn("mic_info", LuaEngineMic::l_mic_info);
    registerFn("mic_monitor", LuaEngineMic::l_mic_monitor);
    registerFn("mic_level", LuaEngineMic::l_mic_level);
    registerFn("mic_has_signal", LuaEngineMic::l_mic_has_signal);
    registerFn("mic_set_gain", LuaEngineMic::l_mic_set_gain);
    registerFn("mic_gain", LuaEngineMic::l_mic_gain);
    registerFn("mic_set_rate", LuaEngineMic::l_mic_set_rate);
    registerFn("mic_rate", LuaEngineMic::l_mic_rate);
}
