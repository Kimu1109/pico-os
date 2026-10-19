#include "sound/Music_Service.hpp"

#include "system/Background_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"

#include <climits>
#include <cstdlib>
#include <cstring>
#include <new>

using MusicService::Track;
using MusicService::Kind;

namespace {
    // 1つ前の曲へ戻らず、今の曲の頭へ戻す境目
    constexpr uint32_t kRestartMs = 3000;
    // 飛ばした曲の理由を出しておく時間
    constexpr uint32_t kErrorShowMs = 8000;

    // 鳴らしている間だけ持つもの(最初に鳴らしたときに malloc)
    struct State {
        FixedString<PICO_STR_M> folders[MusicService::kMaxFolders];
        int folder_count = 0;
        Track tracks[MusicService::kMaxTracks];
        int playlist = INT_MIN;
        FixedString<PICO_STR_M> playlist_name;
        PlayQueue queue;

        Kind kind = Kind::None;
        uint32_t serial = 0;            // 鳴らした直後の SoundFunctions::PlaybackSerial()
        bool finished = false;
        bool interrupted = false;
        int fail_streak = 0;
        uint64_t failed_mask = 0;
        FixedString<PICO_STR_M> error_track;
        FixedString<PICO_STR_256B> error_detail;
        bool error_sticky = false;
        uint32_t error_until_ms = 0;
    };
    State* st = nullptr;

    // 状態が無くても見せる設定(music.cfg)
    bool shuffle = false;
    PlayQueue::Repeat repeat = PlayQueue::Repeat::Off;
    bool settings_loaded = false;
    uint32_t revision = 0;
    uint32_t rng_seed = 0x5A17u;

    bool EndsWith(const char* name, const char* ext){
        const size_t n = strlen(name), e = strlen(ext);
        if(n < e) return false;
        for(size_t i = 0; i < e; i++){
            char c = name[n - e + i];
            if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if(c != ext[i]) return false;
        }
        return true;
    }

    void EnsureSettings(){
        if(!settings_loaded) MusicService::LoadSettings();
    }

    void SaveSettings(){
        if(!OSData::SD_usable) return;
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_MUSIC_CFG, "shuffle", PICO_Config::ConfigValue::FromBool(shuffle));
        PICO_Config::SetValue(PICO_Path::FILE::CFG::SYS_MUSIC_CFG, "repeat",
                              repeat == PlayQueue::Repeat::All ? "all" : (repeat == PlayQueue::Repeat::One ? "one" : "off"));
    }

    bool EnsureState(){
        if(st) return true;
        st = new (std::nothrow) State();
        if(!st){
            LOG_APP_FAIL("ミュージック: 再生のためのメモリ(%uB)が足りません", (unsigned)sizeof(State));
            return false;
        }
        EnsureSettings();
        st->queue.setShuffle(shuffle);
        st->queue.setRepeat(repeat);
        st->queue.seed(millis() ^ rng_seed);
        return true;
    }

    void StopSound(){
        if(!st) return;
        //自分の曲がまだ鳴っているときだけ止める(他のアプリが鳴らした音を止めないように)
        if(st->kind != Kind::None && !st->interrupted && SoundFunctions::PlaybackSerial() == st->serial){
            SoundFunctions::MusicStop();
            SoundFunctions::WavStop();
        }
        st->kind = Kind::None;
    }

    bool TryPlay(int track, FixedString<PICO_STR_256B>& error){
        const Track& t = st->tracks[track];
        FixedString<PICO_PATH_LEN> path;
        path.assign(PICO_Path::DIR::MUSIC);
        if(t.folder >= 0 && t.folder < st->folder_count){
            path.append(st->folders[(int)t.folder].c_str());
            path.append("/");
        }
        path.append(t.name.c_str());

        //画面が周波数を望んでいない(ホーム画面のウィジェット等から鳴らした)なら、先に44100Hzへ切り替えておく。
        //鳴らした後の次のフレームで main.cpp がバックグラウンドの望み(44100Hz)へ切り替えると、その切り替えで
        //今鳴らした曲が止まり「他のアプリに取られた」扱いになってしまうため
        if(SoundFunctions::RequestedSampleRate() == 0) SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateHigh);

        //MMLとWAVは同時には鳴らさない(このアプリで鳴らすのは1曲ずつ)
        if(MusicService::IsWav(t.name.c_str())){
            const char* err = "";
            if(!SoundFunctions::WavPlay(path.c_str(), false, 100, &err)){
                error.assign(err && *err ? err : "WAVとして読めません");
                return false;
            }
            SoundFunctions::MusicStop();
            st->kind = Kind::Wav;
        }else{
            MmlResult r;
            if(!SoundFunctions::MusicPlayFile(path.c_str(), &r)){
                error.assign("");
                if(r.line > 0) error.appendFormat("%d行%d列: ", r.line, r.col);
                error.append(r.message.c_str());
                return false;
            }
            SoundFunctions::WavStop();
            st->kind = Kind::Mml;
        }
        st->serial = SoundFunctions::PlaybackSerial();
        st->interrupted = false;
        return true;
    }

    // track を鳴らす。読めなければ、skip_on_error なら理由を少し出して次へ飛ばし、
    // そうでなければ止まって理由を出し続ける(利用者が選んだ曲)
    void PlayAt(int track, bool skip_on_error){
        const int n = st->queue.count();
        while(track >= 0){
            FixedString<PICO_STR_256B> err;
            if(TryPlay(track, err)){
                st->fail_streak = 0;
                st->finished = false;
                st->error_sticky = false;
                st->failed_mask &= ~((uint64_t)1 << track);
                break;
            }
            LOG_APP_WARN("ミュージック: %s を読めません: %s", st->tracks[track].name.c_str(), err.c_str());
            st->failed_mask |= (uint64_t)1 << track;
            st->error_track.assign(st->tracks[track].name.c_str());
            st->error_detail = err;
            if(!skip_on_error){
                StopSound();
                st->error_sticky = true;
                st->error_until_ms = 0;
                break;
            }
            st->error_sticky = false;
            st->error_until_ms = millis() + kErrorShowMs;
            //全部読めなければ止まる
            if(++st->fail_streak >= n){
                StopSound();
                st->error_sticky = true;
                break;
            }
            track = st->queue.next(true);
            if(track < 0){
                StopSound();
                st->finished = true;
                break;
            }
        }
        revision++;
    }

    void Advance(bool by_user){
        if(!st || st->queue.count() == 0) return;
        const int next = st->queue.next(by_user);
        if(next < 0){
            //リピートしない: 最後まで鳴らした。次に再生を押したら頭から
            StopSound();
            st->finished = true;
            revision++;
            return;
        }
        //曲の終わりで進んだ先が読めなければ飛ばす。「次へ」で選んだ曲なら止まって理由を出す
        PlayAt(next, !by_user);
    }

    bool SoundActive(){
        if(!st || st->interrupted) return false;
        if(st->kind == Kind::Wav) return SoundFunctions::WavPlaying();
        if(st->kind == Kind::Mml) return SoundFunctions::MusicPlaying();
        return false;
    }

    // ---- BackgroundFunctions へ見せる口 ----
    class Service : public BackgroundService {
        public:
            const char* key() const override { return "music"; }
            const char* name() const override { return "ミュージック"; }
            IconID icon() const override { return IconID::Music; }
            void update(uint32_t) override { MusicService::Update(); }
            bool active() const override { return MusicService::Active(); }
            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!st || st->queue.count() == 0 || st->queue.current() < 0){
                    out.assign("止まっています");
                    return;
                }
                FixedString<PICO_PATH_LEN> name;
                MusicService::DisplayName(st->tracks[st->queue.current()].name.c_str(), name);
                if(MusicService::Active()){
                    out.appendFormat("%s%d/%d %s", MusicService::Paused() ? "一時停止 " : "",
                                     st->queue.position() + 1, st->queue.count(), name.c_str());
                }else if(st->interrupted){
                    out.assign("他のアプリが音を使ったため止まりました");
                }else{
                    out.appendFormat("停止中 %s", name.c_str());
                }
            }
            void stop() override { MusicService::Release(); }
            const char* appName() const override { return "ミュージック"; }
            uint32_t preferredSampleRate() const override { return SoundFunctions::kSampleRateHigh; }
            //曲が終わって次へ進むまでの一瞬も周波数の要求を保つ(さもないと22050Hzへ切り替わって次の曲が止まる)。
            //他のアプリに取られた後も、そのアプリの曲/WAVが鳴っている間は保つ(手放すと周波数の切り替えで
            //取った側の音まで止まる。動画アプリの音が鳴らなくなっていた)。鳴り終われば手放す
            bool holdsSampleRate() const override {
                if(!st) return false;
                if(st->interrupted) return SoundFunctions::WavPlaying() || SoundFunctions::MusicPlaying();
                return st->kind != Kind::None;
            }
    };
    Service service;
}

// ================================================================ 操作

bool MusicService::StartPlaylist(const Track* tracks, int count,
                                 const FixedString<PICO_STR_M>* folders, int folder_count,
                                 int playlist, const char* playlist_name, int track){
    if(count <= 0) return false;
    if(!EnsureState()) return false;
    if(count > kMaxTracks) count = kMaxTracks;
    if(folder_count > kMaxFolders) folder_count = kMaxFolders;
    if(folder_count < 0) folder_count = 0;
    for(int i = 0; i < folder_count; i++) st->folders[i] = folders[i];
    st->folder_count = folder_count;
    for(int i = 0; i < count; i++) st->tracks[i] = tracks[i];
    st->playlist = playlist;
    st->playlist_name.assign(playlist_name ? playlist_name : "");
    st->queue.reset(count);
    st->fail_streak = 0;
    st->failed_mask = 0;
    st->finished = false;
    //全曲再生(track<0)は並びの頭から。読めない曲は飛ばす
    const bool all = track < 0;
    if(!all) st->queue.start(track);
    PlayAt(st->queue.current(), all);
    return true;
}

void MusicService::PlayTrack(int track){
    if(!st || track < 0 || track >= st->queue.count()) return;
    st->queue.start(track);
    st->fail_streak = 0;
    PlayAt(st->queue.current(), false);
}

void MusicService::Next(){
    if(!st || st->queue.count() == 0) return;
    st->fail_streak = 0;
    Advance(true);
}

void MusicService::Prev(){
    if(!st || st->queue.count() == 0) return;
    st->fail_streak = 0;
    //鳴らし始めて少し経っていれば今の曲の頭へ
    if(SoundActive()){
        if(st->kind == Kind::Wav && SoundFunctions::WavPositionMs() > kRestartMs){
            SoundFunctions::WavSeekMs(0);
            return;
        }
        if(st->kind == Kind::Mml && SoundFunctions::MusicElapsedMs() > kRestartMs){
            PlayAt(st->queue.current(), false);
            return;
        }
    }
    PlayAt(st->queue.prev(), false);
}

void MusicService::TogglePlay(){
    if(!st || st->queue.count() == 0) return;
    if(SoundActive()){
        if(st->kind == Kind::Wav) SoundFunctions::WavPause(!SoundFunctions::WavPaused());
        else                      SoundFunctions::MusicPause(!SoundFunctions::MusicPaused());
        revision++;
        return;
    }
    if(st->finished){
        st->queue.start(st->queue.orderAt(0));
        st->finished = false;
    }
    st->fail_streak = 0;
    PlayAt(st->queue.current(), false);
}

void MusicService::Stop(){
    if(!st) return;
    StopSound();
    revision++;
}

void MusicService::SeekMs(uint32_t ms){
    if(st && st->kind == Kind::Wav && !st->interrupted) SoundFunctions::WavSeekMs(ms);
}

void MusicService::SetShuffle(bool on){
    EnsureSettings();
    shuffle = on;
    if(st) st->queue.setShuffle(on);
    SaveSettings();
    revision++;
}

void MusicService::CycleRepeat(){
    EnsureSettings();
    repeat = repeat == PlayQueue::Repeat::Off ? PlayQueue::Repeat::All
           : (repeat == PlayQueue::Repeat::All ? PlayQueue::Repeat::One : PlayQueue::Repeat::Off);
    if(st) st->queue.setRepeat(repeat);
    SaveSettings();
    revision++;
}

void MusicService::Release(){
    if(!st) return;
    StopSound();
    delete st;
    st = nullptr;
    revision++;
}

void MusicService::Update(){
    if(!st || st->kind == Kind::None) return;
    //他のアプリが曲/WAVを鳴らした(周波数の切り替えで止まった)。自分の曲は差し替えられたので進まない
    if(SoundFunctions::PlaybackSerial() != st->serial){
        LOG_APP_MSG("ミュージック: 他のアプリが音を使ったため再生を止めました");
        st->kind = Kind::None;
        st->interrupted = true;
        revision++;
        return;
    }
    const bool wav = st->kind == Kind::Wav;
    if(!(wav ? SoundFunctions::WavPlaying() : SoundFunctions::MusicPlaying())){
        //SDが使えない(USBドライブでPCへ貸している)間は次の曲を読めないので、そこで止める(全部を「読めない」にしない)
        if(!OSData::SD_usable){
            StopSound();
            revision++;
            return;
        }
        Advance(false);
    }else if(!wav && st->queue.repeat() != PlayQueue::Repeat::One && !SoundFunctions::MusicPaused()){
        //終わりの無い曲(L で戻る)は1周したら次へ
        bool loops = false;
        const uint32_t total = SoundFunctions::MusicTotalMs(&loops);
        if(loops && total > 0 && SoundFunctions::MusicElapsedMs() >= total) Advance(false);
    }
}

// ================================================================ 読む

bool MusicService::HasQueue(){ return st && st->queue.count() > 0; }
bool MusicService::Active(){ return SoundActive(); }
bool MusicService::Paused(){
    if(!SoundActive()) return false;
    return st->kind == Kind::Wav ? SoundFunctions::WavPaused() : SoundFunctions::MusicPaused();
}
bool MusicService::Finished(){ return st && st->finished; }
Kind MusicService::PlayingKind(){ return (st && !st->interrupted) ? st->kind : Kind::None; }
int MusicService::Count(){ return st ? st->queue.count() : 0; }
int MusicService::Current(){ return st ? st->queue.current() : -1; }
int MusicService::Position(){ return st ? st->queue.position() : 0; }
const Track* MusicService::TrackAt(int i){
    if(!st || i < 0 || i >= st->queue.count()) return nullptr;
    return &st->tracks[i];
}
int MusicService::Playlist(){ return st ? st->playlist : INT_MIN; }
const char* MusicService::PlaylistName(){ return st ? st->playlist_name.c_str() : ""; }
bool MusicService::Shuffle(){ EnsureSettings(); return shuffle; }
PlayQueue::Repeat MusicService::Repeat(){ EnsureSettings(); return repeat; }
uint64_t MusicService::FailedMask(){ return st ? st->failed_mask : 0; }
bool MusicService::ErrorShown(){
    if(!st) return false;
    if(st->error_sticky) return true;
    return st->error_until_ms != 0 && (int32_t)(millis() - st->error_until_ms) < 0;
}
bool MusicService::ErrorSticky(){ return st && st->error_sticky; }
const char* MusicService::ErrorTrack(){ return st ? st->error_track.c_str() : ""; }
const char* MusicService::ErrorDetail(){ return st ? st->error_detail.c_str() : ""; }
bool MusicService::Interrupted(){ return st && st->interrupted; }
uint32_t MusicService::Revision(){ return revision; }

void MusicService::DisplayName(const char* file, FixedString<PICO_PATH_LEN>& out){
    const char* dot = strrchr(file, '.');
    out.assign(file, dot ? (size_t)(dot - file) : strlen(file));
}
bool MusicService::IsMml(const char* name){ return EndsWith(name, ".mml"); }
bool MusicService::IsWav(const char* name){ return EndsWith(name, ".wav"); }

BackgroundService* MusicService::Service(){ return &service; }

void MusicService::LoadSettings(){
    settings_loaded = true;
    shuffle = false;
    repeat = PlayQueue::Repeat::Off;
    //無くてよい(無ければ既定: 順番通り・リピートしない)
    if(OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_MUSIC_CFG)) PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_MUSIC_CFG, [&](const char* key, const char* value){
        if(strcmp(key, "shuffle") == 0){
            bool b;
            if(PICO_Config::ConfigValue::AsBool(value, b)) shuffle = b;
        }else if(strcmp(key, "repeat") == 0){
            if(strcmp(value, "all") == 0) repeat = PlayQueue::Repeat::All;
            else if(strcmp(value, "one") == 0) repeat = PlayQueue::Repeat::One;
            else repeat = PlayQueue::Repeat::Off;
        }
    });
    if(st){
        st->queue.setShuffle(shuffle);
        st->queue.setRepeat(repeat);
    }
}

void MusicService::Test::Reset(){
    delete st;
    st = nullptr;
    shuffle = false;
    repeat = PlayQueue::Repeat::Off;
    settings_loaded = false;
    revision++;
}
