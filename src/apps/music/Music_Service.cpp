#include "apps/music/Music_Service.hpp"
#include "apps/music/Music_Library.hpp"

#include "system/Background_Functions.hpp"
#include "hal/Sound_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Mml_Compiler.hpp"
#include "sound/Audio_Format.hpp"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "Arduino.h"
#include "system/I18n_Functions.hpp"

#include <climits>
#include <cstdio>
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
        FixedString<PICO_STR_M> playlist_key;   // 「続きから」で並びを作り直すための名前(フォルダ名・タグの名前)
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

        // 曲と曲の間を空けない: 次の曲を SoundFunctions へ予約しておく(WavQueueNext)。
        // 順番は写しで先に進めておき、耳に届いた(WavAdvanceCount が増えた)ときに本物へ移す
        bool next_planned = false;      // 今の曲の次はもう決めた(1曲に1回。ミックスの一巡で混ぜ直しが毎回変わらないように)
        int next_track = -1;            // 予約した曲(無ければ-1)
        PlayQueue next_queue;           // 予約した曲へ進めた後の順番
        uint32_t advance_seen = 0;      // 最後に見た SoundFunctions::WavAdvanceCount()
    };
    State* st = nullptr;

    // 状態が無くても見せる設定(music.cfg)
    bool shuffle = false;
    PlayQueue::Repeat repeat = PlayQueue::Repeat::Off;
    bool settings_loaded = false;
    uint32_t revision = 0;

    // スリープタイマー
    MusicService::SleepMode sleep_mode = MusicService::SleepMode::Off;
    uint32_t sleep_deadline_ms = 0;
    bool sleep_fading = false;

    // ---- 続きから ----
    constexpr uint32_t kResumeSaveMs = 10000;
    struct ResumeInfo {
        bool loaded = false;    // music_resume.cfg を読んだ
        bool valid = false;
        int playlist = MusicService::kPlaylistAll;
        FixedString<PICO_STR_M> key;        // フォルダ名・タグの名前
        FixedString<PICO_STR_M> folder;     // 曲のフォルダ名(直下なら空)
        FixedString<PICO_STR_M> file;
        FixedString<PICO_STR_M> title;
        uint32_t ms = 0;
    };
    ResumeInfo resume;
    uint32_t resume_saved_at = 0;
    int resume_saved_track = -1;
    uint32_t resume_saved_sec = 0xFFFFFFFF;

    const char* PlaylistKindName(int playlist){
        if(playlist == MusicService::kPlaylistArtist) return "artist";
        if(playlist == MusicService::kPlaylistAlbum) return "album";
        if(playlist >= 0) return "folder";
        return "all";
    }

    void LoadResume(){
        if(resume.loaded) return;
        resume.loaded = true;
        resume.valid = false;
        if(!OSData::SD_usable || !OSData::SD.exists(PICO_Path::FILE::CFG::SYS_MUSIC_RESUME_CFG)) return;
        int kind = INT_MIN;
        PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_MUSIC_RESUME_CFG, [&](const char* key, const char* value){
            if(strcmp(key, "list") == 0){
                if(strcmp(value, "all") == 0) kind = MusicService::kPlaylistAll;
                else if(strcmp(value, "folder") == 0) kind = 0;
                else if(strcmp(value, "artist") == 0) kind = MusicService::kPlaylistArtist;
                else if(strcmp(value, "album") == 0) kind = MusicService::kPlaylistAlbum;
            }
            else if(strcmp(key, "name") == 0){ if(!resume.key.assign(value)) resume.key.clear(); }
            else if(strcmp(key, "folder") == 0){ if(!resume.folder.assign(value)) resume.folder.clear(); }
            else if(strcmp(key, "file") == 0){ if(!resume.file.assign(value)) resume.file.clear(); }
            else if(strcmp(key, "title") == 0){ if(!resume.title.assign(value)) resume.title.clear(); }
            else if(strcmp(key, "position-ms") == 0) resume.ms = (uint32_t)strtoul(value, nullptr, 10);
        });
        resume.playlist = kind;
        resume.valid = kind != INT_MIN && !resume.file.empty();
    }

    void ClearResume(){
        resume.loaded = true;
        if(!resume.valid) return;
        resume.valid = false;
        if(OSData::SD_usable) OSData::SD.remove(PICO_Path::FILE::CFG::SYS_MUSIC_RESUME_CFG);
    }

    // 今の曲と位置を書く(一時ファイルへ書いてから差し替える。書きかけで電源が切れても前の内容が残る)
    void SaveResume(uint32_t ms){
        if(!st || st->queue.count() == 0 || st->queue.current() < 0 || !OSData::SD_usable) return;
        const Track& t = st->tracks[st->queue.current()];
        resume.loaded = true;
        resume.valid = true;
        resume.playlist = st->playlist;
        resume.key = st->playlist_key;
        if(!resume.folder.assign(t.folder >= 0 && t.folder < st->folder_count ? st->folders[(int)t.folder].c_str() : "")) resume.folder.clear();
        resume.file = t.name;
        resume.title = t.title;
        resume.ms = ms;
        resume_saved_at = millis();

        static constexpr const char* kTmp = "/sys/music_resume.tmp";
        FsFile f = OSData::SD.open(kTmp, O_WRONLY | O_CREAT | O_TRUNC);
        if(!f){
            LOG_APP_WARN("ミュージック: 続きの場所を書けません");
            return;
        }
        char num[16];
        snprintf(num, sizeof(num), "%lu", (unsigned long)ms);
        bool ok = PICO_Config::Detail::WriteEntry(f, "list", PlaylistKindName(resume.playlist));
        ok = ok && PICO_Config::Detail::WriteEntry(f, "name", resume.key.c_str());
        ok = ok && PICO_Config::Detail::WriteEntry(f, "folder", resume.folder.c_str());
        ok = ok && PICO_Config::Detail::WriteEntry(f, "file", resume.file.c_str());
        ok = ok && PICO_Config::Detail::WriteEntry(f, "title", resume.title.c_str());
        ok = ok && PICO_Config::Detail::WriteEntry(f, "position-ms", num);
        f.close();
        if(!ok){ OSData::SD.remove(kTmp); return; }
        OSData::SD.remove(PICO_Path::FILE::CFG::SYS_MUSIC_RESUME_CFG);
        OSData::SD.rename(kTmp, PICO_Path::FILE::CFG::SYS_MUSIC_RESUME_CFG);
    }

    // 今鳴っている位置(WAV/MP3。MMLは曲の頭から鳴らし直すので0)
    uint32_t CurrentPositionMs(){
        if(!st || st->interrupted || st->kind != Kind::Wav) return 0;
        return SoundFunctions::WavPositionMs();
    }

    void ResetSleep(){
        if(sleep_mode == MusicService::SleepMode::Off) return;
        sleep_mode = MusicService::SleepMode::Off;
        //小さくしていた途中なら戻す(次の WavPlay() も戻すが、止めずに取り消したときのため)
        if(sleep_fading) SoundFunctions::WavSetVolume(100);
        sleep_fading = false;
        revision++;
    }
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
        st->next_planned = false;
        st->next_track = -1;
        ResetSleep();
    }

    // 曲のSD上の場所。長すぎて入らなければ false
    bool TrackPath(int track, FixedString<PICO_PATH_LEN>& path){
        const Track& t = st->tracks[track];
        bool ok = path.assign(PICO_Path::DIR::MUSIC);
        if(t.folder >= 0 && t.folder < st->folder_count){
            ok = ok && path.append(st->folders[(int)t.folder].c_str());
            ok = ok && path.append("/");
        }
        return ok && path.append(t.name.c_str());
    }

    // 次の曲の予約を取り消す(ミックス・リピートを変えた・別の曲を鳴らした)。もう続けて積んだ後なら、その予約のまま進む
    void DropNextPlan(){
        if(!st || !st->next_planned) return;
        if(st->next_track >= 0 && !SoundFunctions::WavClearNext()) return;
        st->next_planned = false;
        st->next_track = -1;
    }

    // 今のWAV/MP3の曲の次を決めて SoundFunctions へ予約する(次もWAV/MP3のときだけ。MMLは今までどおり間を空けて鳴らす)
    void PlanNext(){
        st->next_planned = true;
        st->next_track = -1;
        if(!OSData::SD_usable) return;
        //「この曲の終わりで止める」なら次は積まない
        if(sleep_mode == MusicService::SleepMode::TrackEnd) return;
        PlayQueue q = st->queue;
        const int n = q.next(false);
        if(n < 0 || !MusicService::IsPcmFile(st->tracks[n].name.c_str())) return;
        FixedString<PICO_PATH_LEN> path;
        if(!TrackPath(n, path)) return;
        if(!SoundFunctions::WavQueueNext(path.c_str())) return;
        st->next_queue = q;
        st->next_track = n;
    }

    bool TryPlay(int track, FixedString<PICO_STR_256B>& error){
        const Track& t = st->tracks[track];
        FixedString<PICO_PATH_LEN> path;
        if(!TrackPath(track, path)){
            error.assign(TR("読めない音のファイルです"));
            return false;
        }

        //画面が周波数を望んでいない(ホーム画面のウィジェット等から鳴らした)なら、先に44100Hzへ切り替えておく。
        //鳴らした後の次のフレームで main.cpp がバックグラウンドの望み(44100Hz)へ切り替えると、その切り替えで
        //今鳴らした曲が止まり「他のアプリに取られた」扱いになってしまうため
        if(SoundFunctions::RequestedSampleRate() == 0) SoundFunctions::RequestSampleRate(SoundFunctions::kSampleRateHigh);

        //MMLとWAV/MP3は同時には鳴らさない(このアプリで鳴らすのは1曲ずつ)
        if(MusicService::IsPcmFile(t.name.c_str())){
            const char* err = "";
            if(!SoundFunctions::WavPlay(path.c_str(), false, 100, &err)){
                error.assign(err && *err ? err : TR("読めない音のファイルです"));
                return false;
            }
            SoundFunctions::MusicStop();
            st->kind = Kind::Wav;
            //WavPlay() が予約を消したので、次はこの曲が鳴り始めてから決める
            st->next_planned = false;
            st->next_track = -1;
            st->advance_seen = SoundFunctions::WavAdvanceCount();
        }else{
            MmlResult r;
            if(!SoundFunctions::MusicPlayFile(path.c_str(), &r)){
                error.assign("");
                if(r.line > 0) error.appendFormat(TR("%d行%d列: "), r.line, r.col);
                error.append(r.message.c_str());
                return false;
            }
            SoundFunctions::WavStop();
            st->kind = Kind::Mml;
        }
        st->serial = SoundFunctions::PlaybackSerial();
        st->interrupted = false;
        resume_saved_track = -1;    //次の Update() で続きの場所を書く(新しい並びで同じ番号の曲でも)
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
                ClearResume();
                break;
            }
        }
        revision++;
    }

    void Advance(bool by_user){
        if(!st || st->queue.count() == 0) return;
        //「この曲の終わりで止める」: 曲の終わりで進むときは止める(利用者の「次へ」は進む)
        if(!by_user && sleep_mode == MusicService::SleepMode::TrackEnd){
            LOG_APP_MSG("ミュージック: スリープタイマー(曲の終わり)で止めました");
            //次に鳴らすのは次の曲(鳴らさずに順番だけ進め、続きの場所もそこにする)
            if(st->queue.next(false) >= 0) SaveResume(0);
            StopSound();
            revision++;
            return;
        }
        const int next = st->queue.next(by_user);
        if(next < 0){
            //リピートしない: 最後まで鳴らした。次に再生を押したら頭から(続きは無い)
            StopSound();
            st->finished = true;
            ClearResume();
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
            const char* name() const override { return TR("ミュージック"); }
            IconID icon() const override { return IconID::Music; }
            void update(uint32_t) override { MusicService::Update(); }
            bool active() const override { return MusicService::Active(); }
            void status(FixedString<PICO_STR_LL>& out) const override {
                out.clear();
                if(!st || st->queue.count() == 0 || st->queue.current() < 0){
                    out.assign(TR("止まっています"));
                    return;
                }
                FixedString<PICO_PATH_LEN> name;
                MusicService::TrackTitle(st->tracks[st->queue.current()], name);
                if(MusicService::Active()){
                    out.appendFormat("%s%d/%d %s", MusicService::Paused() ? TR("一時停止 ") : "",
                                     st->queue.position() + 1, st->queue.count(), name.c_str());
                }else if(st->interrupted){
                    out.assign(TR("他のアプリが音を使ったため止まりました"));
                }else{
                    out.appendFormat(TR("停止中 %s"), name.c_str());
                }
            }
            void stop() override { MusicService::Release(); }
            const char* appName() const override { return TR("ミュージック"); }
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
                                 int playlist, const char* playlist_name, int track,
                                 const char* playlist_key){
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
    if(!st->playlist_key.assign(playlist_key ? playlist_key : (playlist_name ? playlist_name : ""))) st->playlist_key.clear();
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
    //順番が無ければ(起動した後)、前回の続きから
    if(!st || st->queue.count() == 0){
        if(HasResume()) Resume();
        return;
    }
    if(SoundActive()){
        //止める前の位置を書いておく(一時停止のまま電源を切っても、ここから)
        if(!Paused()) SaveResume(CurrentPositionMs());
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
    if(SoundActive()) SaveResume(CurrentPositionMs());
    StopSound();
    revision++;
}

void MusicService::SeekMs(uint32_t ms){
    if(st && st->kind == Kind::Wav && !st->interrupted) SoundFunctions::WavSeekMs(ms);
}

void MusicService::SetShuffle(bool on){
    EnsureSettings();
    shuffle = on;
    DropNextPlan();
    if(st) st->queue.setShuffle(on);
    SaveSettings();
    revision++;
}

void MusicService::CycleRepeat(){
    EnsureSettings();
    repeat = repeat == PlayQueue::Repeat::Off ? PlayQueue::Repeat::All
           : (repeat == PlayQueue::Repeat::All ? PlayQueue::Repeat::One : PlayQueue::Repeat::Off);
    DropNextPlan();
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
    //スリープタイマー: 最後の kSleepFadeMs はWAV/MP3を小さくしていき、時間になったら止める
    if(sleep_mode == SleepMode::Timer && !st->interrupted && SoundFunctions::PlaybackSerial() == st->serial){
        const int32_t left = (int32_t)(sleep_deadline_ms - millis());
        if(left <= 0){
            LOG_APP_MSG("ミュージック: スリープタイマーで止めました");
            SaveResume(CurrentPositionMs());
            StopSound();
            revision++;
            return;
        }
        if(st->kind == Kind::Wav && (uint32_t)left < kSleepFadeMs){
            sleep_fading = true;
            SoundFunctions::WavSetVolume((uint8_t)((uint64_t)left * 100 / kSleepFadeMs));
        }
    }
    //他のアプリが曲/WAVを鳴らした(周波数の切り替えで止まった)。自分の曲は差し替えられたので進まない
    if(SoundFunctions::PlaybackSerial() != st->serial){
        LOG_APP_MSG("ミュージック: 他のアプリが音を使ったため再生を止めました");
        //自分のWAVが鳴り続けている(他のアプリがMMLを鳴らした)なら、次の曲へは続けない
        if(st->kind == Kind::Wav) SoundFunctions::WavClearNext();
        //スリープタイマーも取り消す(相手の音の音量には触らない)
        sleep_fading = false;
        ResetSleep();
        st->next_planned = false;
        st->next_track = -1;
        st->kind = Kind::None;
        st->interrupted = true;
        revision++;
        return;
    }
    const bool wav = st->kind == Kind::Wav;
    //予約した次の曲が耳に届いた(間を空けずに続けて鳴った)。順番を進める
    if(wav && SoundFunctions::WavAdvanceCount() != st->advance_seen){
        st->advance_seen = SoundFunctions::WavAdvanceCount();
        if(st->next_track >= 0){
            st->queue = st->next_queue;
            st->failed_mask &= ~((uint64_t)1 << st->next_track);
            st->fail_streak = 0;
            st->finished = false;
            st->error_sticky = false;
        }
        st->next_planned = false;
        st->next_track = -1;
        revision++;
    }
    if(wav && !st->next_planned && SoundFunctions::WavPlaying()) PlanNext();
    //続きの場所: 曲が変わった・鳴らしている間は約10秒ごと(電源を急に切っても、ずれは10秒まで)
    {
        const int cur = st->queue.current();
        const bool paused = wav ? SoundFunctions::WavPaused() : SoundFunctions::MusicPaused();
        if(cur != resume_saved_track || (!paused && (uint32_t)(millis() - resume_saved_at) >= kResumeSaveMs)){
            const uint32_t ms = CurrentPositionMs();
            if(cur != resume_saved_track || ms / 1000 != resume_saved_sec){
                SaveResume(ms);
                resume_saved_sec = ms / 1000;
            }
            resume_saved_track = cur;
            resume_saved_at = millis();
        }
    }
    if(!(wav ? SoundFunctions::WavPlaying() : SoundFunctions::MusicPlaying())){
        //続けて積めなかった(読めない・チャンネル数が違う)。今までどおり次を鳴らす
        st->next_planned = false;
        st->next_track = -1;
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

void MusicService::SetSleepTimer(uint32_t minutes){
    if(minutes == 0){ ClearSleepTimer(); return; }
    if(sleep_fading) SoundFunctions::WavSetVolume(100);
    sleep_fading = false;
    sleep_mode = SleepMode::Timer;
    sleep_deadline_ms = millis() + minutes * 60000u;
    revision++;
}

void MusicService::SetSleepAtTrackEnd(){
    if(sleep_fading) SoundFunctions::WavSetVolume(100);
    sleep_fading = false;
    sleep_mode = SleepMode::TrackEnd;
    //もう次の曲を予約していれば取り消す(続けて積んだ後なら、その次の曲の終わりで止まる)
    if(st && st->next_planned){
        if(st->next_track < 0 || SoundFunctions::WavClearNext()){
            st->next_planned = false;
            st->next_track = -1;
        }
    }
    revision++;
}

void MusicService::ClearSleepTimer(){
    ResetSleep();
    //「曲の終わりで」を取り消したら、次の曲を決め直す
    if(st) DropNextPlan();
}

MusicService::SleepMode MusicService::SleepTimerMode(){ return sleep_mode; }

uint32_t MusicService::SleepRemainingMs(){
    if(sleep_mode != SleepMode::Timer) return 0;
    const int32_t left = (int32_t)(sleep_deadline_ms - millis());
    return left > 0 ? (uint32_t)left : 0;
}

// ================================================================ 続きから

bool MusicService::HasResume(){
    LoadResume();
    return resume.valid;
}

const char* MusicService::ResumeTitle(){
    LoadResume();
    if(!resume.valid) return "";
    if(!resume.title.empty()) return resume.title.c_str();
    static FixedString<PICO_PATH_LEN> name;
    DisplayName(resume.file.c_str(), name);
    return name.c_str();
}

uint32_t MusicService::ResumePositionMs(){
    LoadResume();
    return resume.valid ? resume.ms : 0;
}

bool MusicService::Resume(){
    LoadResume();
    if(!resume.valid || !OSData::SD_usable) return false;
    //並びを作り直す間だけ持つ(約7KB。1コア目のスタックには置けない)
    struct Work {
        FixedString<PICO_STR_M> folders[kMaxFolders];
        Track tracks[kMaxTracks];
    };
    Work* w = new (std::nothrow) Work();
    if(!w){
        LOG_APP_WARN("ミュージック: 続きから鳴らすためのメモリが足りません");
        return false;
    }
    const int folder_count = MusicLibrary::ScanFolders(w->folders, kMaxFolders);
    int playlist = resume.playlist;
    int count = 0;
    FixedString<PICO_STR_M> name;
    if(playlist == kPlaylistArtist || playlist == kPlaylistAlbum){
        const MusicLibrary::Group g = playlist == kPlaylistArtist ? MusicLibrary::Group::Artist : MusicLibrary::Group::Album;
        count = MusicLibrary::ListGroupTracks(g, resume.key.c_str(), w->folders, folder_count, w->tracks, kMaxTracks);
        GroupDisplayName(playlist, resume.key.c_str(), name);
    }else{
        if(playlist >= 0){
            playlist = -2;
            for(int i = 0; i < folder_count; i++) if(strcmp(w->folders[i].c_str(), resume.key.c_str()) == 0) playlist = i;
        }
        if(playlist == kPlaylistAll || playlist >= 0){
            count = MusicLibrary::ScanTracks(w->folders, folder_count, playlist, w->tracks, kMaxTracks);
            MusicLibrary::FillTitles(w->tracks, count, w->folders, folder_count);
            name.assign(playlist >= 0 ? w->folders[playlist].c_str() : TR("すべての曲"));
        }
    }
    //同じ曲を探す(無くなっていれば並びの頭から)
    int track = -1;
    for(int i = 0; i < count && track < 0; i++){
        const char* folder = w->tracks[i].folder >= 0 ? w->folders[(int)w->tracks[i].folder].c_str() : "";
        if(strcmp(w->tracks[i].name.c_str(), resume.file.c_str()) == 0 && strcmp(folder, resume.folder.c_str()) == 0) track = i;
    }
    bool ok = false;
    if(count > 0){
        const uint32_t ms = track >= 0 ? resume.ms : 0;
        LOG_APP_MSG("ミュージック: 前回の続きから鳴らします(%s %lums)", resume.file.c_str(), (unsigned long)ms);
        ok = StartPlaylist(w->tracks, count, w->folders, folder_count, playlist, name.c_str(), track < 0 ? 0 : track, resume.key.c_str());
        if(ok && ms > 0 && st && st->kind == Kind::Wav) SoundFunctions::WavSeekMs(ms);
    }else{
        LOG_APP_WARN("ミュージック: 前回の曲の並びを作れません(%s)", resume.key.c_str());
    }
    delete w;
    return ok;
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
const char* MusicService::PlaylistKey(){ return st ? st->playlist_key.c_str() : ""; }
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

void MusicService::TrackTitle(const Track& t, FixedString<PICO_PATH_LEN>& out){
    if(!t.title.empty()){ out.assign(t.title.c_str()); return; }
    DisplayName(t.name.c_str(), out);
}

void MusicService::GroupDisplayName(int playlist, const char* key, FixedString<PICO_STR_M>& out){
    if(key && key[0]){ if(!out.assign(key)) out.clear(); return; }
    out.assign(playlist == kPlaylistAlbum ? TR("不明なアルバム") : TR("不明なアーティスト"));
}

void MusicService::DisplayName(const char* file, FixedString<PICO_PATH_LEN>& out){
    const char* dot = strrchr(file, '.');
    out.assign(file, dot ? (size_t)(dot - file) : strlen(file));
}
bool MusicService::IsMml(const char* name){ return EndsWith(name, ".mml"); }
bool MusicService::IsPcmFile(const char* name){ return AudioFormat::IsPcmFile(name); }

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
    resume = ResumeInfo();
    resume_saved_at = 0;
    resume_saved_track = -1;
    resume_saved_sec = 0xFFFFFFFF;
    sleep_mode = SleepMode::Off;
    sleep_fading = false;
    delete st;
    st = nullptr;
    shuffle = false;
    repeat = PlayQueue::Repeat::Off;
    settings_loaded = false;
    revision++;
}
