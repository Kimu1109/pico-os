#include "apps/music/Music_Library.hpp"

#include "sound/Audio_Tags.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <SdFat.h>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
    // 索引のファイルの頭(16バイト)。記録の数は (大きさ - 頭) / 記録の大きさ
    constexpr char kMagic[4] = {'P', 'M', 'I', 'X'};
    constexpr uint32_t kIndexVersion = 1;
    constexpr uint32_t kHeaderBytes = 16;

    uint32_t revision = 0;

    bool IsAudio(const char* name){ return MusicService::IsMml(name) || MusicService::IsWavOrMp3(name); }

    uint32_t Fnv(const char* a, const char* b){
        uint32_t h = 2166136261u;
        for(const char* p = a; *p; p++){ h ^= (uint8_t)*p; h *= 16777619u; }
        h ^= '/'; h *= 16777619u;
        for(const char* p = b; *p; p++){ h ^= (uint8_t)*p; h *= 16777619u; }
        return h ? h : 1;
    }

    bool OpenIndex(FsFile& f, int& count){
        count = 0;
        if(!OSData::SD_usable) return false;
        f = OSData::SD.open(PICO_Path::FILE::CFG::SYS_MUSIC_INDEX, O_RDONLY);
        if(!f) return false;
        uint8_t h[kHeaderBytes];
        uint32_t ver = 0, rec = 0;
        if(f.read(h, sizeof(h)) != (int)sizeof(h) || memcmp(h, kMagic, 4) != 0){ f.close(); return false; }
        memcpy(&ver, h + 4, 4);
        memcpy(&rec, h + 8, 4);
        if(ver != kIndexVersion || rec != sizeof(MusicLibrary::Record)){ f.close(); return false; }
        const uint32_t size = (uint32_t)f.fileSize();
        count = size > kHeaderBytes ? (int)((size - kHeaderBytes) / sizeof(MusicLibrary::Record)) : 0;
        if(count > MusicLibrary::kMaxIndexed) count = MusicLibrary::kMaxIndexed;
        return true;
    }

    // 頭から順に読む(SDを何度も行き来しないよう、続けて read する)
    bool NextRecord(FsFile& f, MusicLibrary::Record& r){
        if(f.read(&r, sizeof(r)) != (int)sizeof(r)) return false;
        //終端が無い記録(壊れた索引)でも文字列として読めるように
        r.folder[sizeof(r.folder) - 1] = r.file[sizeof(r.file) - 1] = '\0';
        r.title[sizeof(r.title) - 1] = r.artist[sizeof(r.artist) - 1] = r.album[sizeof(r.album) - 1] = '\0';
        return true;
    }

    int FolderIndex(const char* name, const FixedString<PICO_STR_M>* folders, int folder_count){
        if(name[0] == '\0') return -1;
        for(int i = 0; i < folder_count; i++) if(strcmp(folders[i].c_str(), name) == 0) return i;
        return -2;  //今は無いフォルダ
    }

    const char* GroupName(const MusicLibrary::Record& r, MusicLibrary::Group g){
        return g == MusicLibrary::Group::Artist ? r.artist : r.album;
    }

    // ---- 作り直す ----
    struct Updater {
        // 前の索引: ハッシュ → 記録の番号(0 = 空き。番号は+1して入れる)。開番地法
        static constexpr int kSlots = 1024;     // kMaxIndexed の2倍
        uint32_t hash[kSlots];
        uint16_t slot_index[kSlots];

        FsFile old_index;       // 前の索引(写すために開いたまま)
        int old_count = 0;
        int old_loaded = 0;     // 表へ入れた数
        FsFile out;             // 新しい索引(.tmp)
        int written = 0;
        int checked = 0;

        FixedString<PICO_STR_M> folders[MusicLibrary::kMaxFolders];
        int folder_count = 0;
        int cur_folder = -1;    // 今見ているフォルダ(-1 = 直下)
        FsFile dir;
        bool dir_open = false;
        bool scanning = false;
    };
    Updater* up = nullptr;

    void Lookup(const char* folder, const char* file, uint32_t size, uint16_t date, uint16_t time, MusicLibrary::Record& r, bool& found){
        found = false;
        if(!up->old_index) return;
        const uint32_t h = Fnv(folder, file);
        for(int k = 0; k < Updater::kSlots; k++){
            const int s = (int)((h + (uint32_t)k) % Updater::kSlots);
            if(up->slot_index[s] == 0) return;
            if(up->hash[s] != h) continue;
            const int i = up->slot_index[s] - 1;
            if(!up->old_index.seek(kHeaderBytes + (uint32_t)i * sizeof(MusicLibrary::Record))) return;
            if(!NextRecord(up->old_index, r)) return;
            if(strcmp(r.folder, folder) == 0 && strcmp(r.file, file) == 0){
                found = r.size == size && r.date == date && r.time == time;
                return;
            }
        }
    }

    // 1曲ぶんの記録を作って書く
    void AddFile(const char* folder, FsFile& file, const char* name){
        if(up->written >= MusicLibrary::kMaxIndexed){
            if(up->written == MusicLibrary::kMaxIndexed) LOG_APP_WARN("ミュージック: 曲が多すぎるため索引に入れません(上限%d)", MusicLibrary::kMaxIndexed);
            up->written++;  //1回だけ知らせる
            return;
        }
        MusicLibrary::Record r;
        const uint32_t size = (uint32_t)file.fileSize();
        uint16_t date = 0, time = 0;
        file.getModifyDateTime(&date, &time);
        bool found = false;
        Lookup(folder, name, size, date, time, r, found);
        if(!found){
            memset(&r, 0, sizeof(r));
            MusicLibrary::CopyUtf8(r.folder, sizeof(r.folder), folder);
            MusicLibrary::CopyUtf8(r.file, sizeof(r.file), name);
            r.size = size; r.date = date; r.time = time;
            FixedString<PICO_PATH_LEN> path;
            bool ok = path.assign(PICO_Path::DIR::MUSIC);
            if(folder[0]){ ok = ok && path.append(folder); ok = ok && path.append("/"); }
            ok = ok && path.append(name);
            AudioTags tags;
            if(ok && AudioTagReader::Read(path.c_str(), tags)){
                MusicLibrary::CopyUtf8(r.title, sizeof(r.title), tags.title.c_str());
                MusicLibrary::CopyUtf8(r.artist, sizeof(r.artist), tags.artist.c_str());
                MusicLibrary::CopyUtf8(r.album, sizeof(r.album), tags.album.c_str());
                r.track = tags.track;
            }
        }
        if(up->out.write(&r, sizeof(r)) != sizeof(r)){
            LOG_APP_WARN("ミュージック: 索引を書けません");
        }
        up->written++;
    }

    void FreeUpdater(bool remove_tmp){
        if(!up) return;
        if(up->dir_open) up->dir.close();
        up->old_index.close();
        up->out.close();
        up->~Updater();
        free(up);
        up = nullptr;
        if(remove_tmp && OSData::SD_usable) OSData::SD.remove(PICO_Path::FILE::CFG::SYS_MUSIC_INDEX_TMP);
    }

    // 次のフォルダを開く。もう無ければ false
    bool OpenNextFolder(){
        while(true){
            if(up->dir_open){ up->dir.close(); up->dir_open = false; }
            up->cur_folder++;
            if(up->cur_folder >= up->folder_count) return false;
            FixedString<PICO_PATH_LEN> path;
            path.assign(PICO_Path::DIR::MUSIC);
            if(up->cur_folder >= 0) path.append(up->folders[up->cur_folder].c_str());
            up->dir = OSData::SD.open(path.c_str());
            if(up->dir){ up->dir_open = true; return true; }
        }
    }
}

void MusicLibrary::CopyUtf8(char* dst, size_t cap, const char* src){
    if(cap == 0) return;
    size_t n = src ? strlen(src) : 0;
    if(n > cap - 1){
        n = cap - 1;
        //続きのバイト(10xxxxxx)で切れていれば、その文字の頭まで戻す
        while(n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
    }
    if(n) memcpy(dst, src, n);
    dst[n] = '\0';
}

// ================================================================ フォルダと曲

int MusicLibrary::ScanFolders(FixedString<PICO_STR_M>* out, int max){
    int count = 0;
    if(!OSData::SD_usable) return 0;
    FsFile dir = OSData::SD.open(PICO_Path::DIR::MUSIC);
    if(!dir) return 0;
    FsFile file;
    while(file.openNext(&dir, O_RDONLY)){
        char name[128];
        const bool ok = file.getName(name, sizeof(name)) && file.isDirectory() && name[0] != '.';
        file.close();
        if(!ok) continue;
        FixedString<PICO_STR_M> n;
        if(!n.assign(name)){
            LOG_APP_WARN("ミュージック: フォルダ名が長すぎるため並べません: %s", name);
            continue;
        }
        if(count >= max){
            LOG_APP_WARN("ミュージック: フォルダが多すぎるため %s を並べません(上限%d)", name, max);
            continue;
        }
        //名前順(SdFatの列挙順は作った順なので並べ替える。高々16件の挿入ソート)
        int i = count++;
        while(i > 0 && strcmp(out[i - 1].c_str(), n.c_str()) > 0){
            out[i] = out[i - 1];
            i--;
        }
        out[i] = n;
    }
    dir.close();
    return count;
}

int MusicLibrary::ScanTracks(const FixedString<PICO_STR_M>* folders, int folder_count, int playlist, Track* out, int max){
    int count = 0;
    if(!OSData::SD_usable) return 0;
    //「すべての曲」は直下と全部のフォルダ、それ以外はそのフォルダだけ
    const int first = playlist < 0 ? -1 : playlist;
    const int last  = playlist < 0 ? folder_count - 1 : playlist;
    for(int f = first; f <= last; f++){
        FixedString<PICO_PATH_LEN> path;
        path.assign(PICO_Path::DIR::MUSIC);
        if(f >= 0) path.append(folders[f].c_str());
        FsFile dir = OSData::SD.open(path.c_str());
        if(!dir) continue;
        FsFile file;
        while(file.openNext(&dir, O_RDONLY)){
            char name[128];
            const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && IsAudio(name);
            file.close();
            if(!ok) continue;
            Track t;
            t.folder = (int8_t)f;
            if(!t.name.assign(name)){
                LOG_APP_WARN("ミュージック: ファイル名が長すぎるため並べません: %s", name);
                continue;
            }
            if(count >= max){
                LOG_APP_WARN("ミュージック: 曲が多すぎるため %s を並べません(上限%d)", name, max);
                continue;
            }
            //名前順の挿入ソート(同じ名前ならフォルダ順)
            int i = count++;
            while(i > 0){
                const int c = strcmp(out[i - 1].name.c_str(), t.name.c_str());
                if(c < 0 || (c == 0 && out[i - 1].folder <= t.folder)) break;
                out[i] = out[i - 1];
                i--;
            }
            out[i] = t;
        }
        dir.close();
    }
    return count;
}

// ================================================================ 索引を読む

int MusicLibrary::IndexCount(){
    FsFile f;
    int count = 0;
    if(!OpenIndex(f, count)) return 0;
    f.close();
    return count;
}

bool MusicLibrary::ReadRecord(int i, Record& out){
    FsFile f;
    int count = 0;
    if(!OpenIndex(f, count)) return false;
    bool ok = i >= 0 && i < count && f.seek(kHeaderBytes + (uint32_t)i * sizeof(Record)) && NextRecord(f, out);
    f.close();
    return ok;
}

int MusicLibrary::ListGroups(Group g, FixedString<PICO_STR_M>* out, int max){
    FsFile f;
    int count = 0, n = 0;
    if(!OpenIndex(f, count)) return 0;
    Record r;
    bool warned = false;
    for(int i = 0; i < count && NextRecord(f, r); i++){
        const char* name = GroupName(r, g);
        //名前順に入れる(同じ名前は1つだけ)
        int lo = 0, hi = n;
        while(lo < hi){
            const int mid = (lo + hi) / 2;
            if(strcmp(out[mid].c_str(), name) < 0) lo = mid + 1; else hi = mid;
        }
        if(lo < n && strcmp(out[lo].c_str(), name) == 0) continue;
        if(n >= max){
            if(!warned) LOG_APP_WARN("ミュージック: %sが多すぎるため並べきれません(上限%d)", g == Group::Artist ? "アーティスト" : "アルバム", max);
            warned = true;
            continue;
        }
        for(int k = n; k > lo; k--) out[k] = out[k - 1];
        if(!out[lo].assign(name)) out[lo].clear();
        n++;
    }
    f.close();
    return n;
}

int MusicLibrary::ListGroupTracks(Group g, const char* name, const FixedString<PICO_STR_M>* folders, int folder_count, Track* out, int max){
    FsFile f;
    int count = 0, n = 0;
    if(!name || !OpenIndex(f, count)) return 0;
    // 並べるための鍵(アーティストの曲はアルバム名でも分ける)。並べる間だけ持つ(約3KB。静的に持つと常にRAMを取るため)
    struct Key { char album[48]; uint16_t track; };
    Key* keys = static_cast<Key*>(malloc(sizeof(Key) * (size_t)max));
    if(!keys){ f.close(); return 0; }
    Record r;
    for(int i = 0; i < count && NextRecord(f, r); i++){
        if(strcmp(GroupName(r, g), name) != 0) continue;
        const int folder = FolderIndex(r.folder, folders, folder_count);
        if(folder == -2) continue;
        if(n >= max){
            LOG_APP_WARN("ミュージック: 曲が多すぎるため %s を並べません(上限%d)", r.file, max);
            continue;
        }
        Track t;
        t.folder = (int8_t)folder;
        if(!t.name.assign(r.file)) continue;
        if(!t.title.assign(r.title)) t.title.clear();
        Key k;
        memcpy(k.album, g == Group::Artist ? r.album : "", g == Group::Artist ? sizeof(k.album) : 1);
        k.album[sizeof(k.album) - 1] = '\0';
        k.track = r.track ? r.track : 0xFFFF;   //曲番号の無い曲は後ろへ
        //挿入ソート: アルバム名 → 曲番号 → ファイル名
        int i2 = n++;
        while(i2 > 0){
            int c = strcmp(keys[i2 - 1].album, k.album);
            if(c == 0) c = (int)keys[i2 - 1].track - (int)k.track;
            if(c == 0) c = strcmp(out[i2 - 1].name.c_str(), t.name.c_str());
            if(c <= 0) break;
            out[i2] = out[i2 - 1];
            keys[i2] = keys[i2 - 1];
            i2--;
        }
        out[i2] = t;
        keys[i2] = k;
    }
    free(keys);
    f.close();
    return n;
}

void MusicLibrary::FillTitles(Track* tracks, int count, const FixedString<PICO_STR_M>* folders, int folder_count){
    for(int i = 0; i < count; i++) tracks[i].title.clear();
    FsFile f;
    int n = 0;
    if(count <= 0 || !OpenIndex(f, n)) return;
    Record r;
    for(int i = 0; i < n && NextRecord(f, r); i++){
        if(r.title[0] == '\0') continue;
        const int folder = FolderIndex(r.folder, folders, folder_count);
        if(folder == -2) continue;
        for(int k = 0; k < count; k++){
            if(tracks[k].folder == folder && strcmp(tracks[k].name.c_str(), r.file) == 0){
                if(!tracks[k].title.assign(r.title)) tracks[k].title.clear();
                break;
            }
        }
    }
    f.close();
}

// ================================================================ 作り直す

bool MusicLibrary::StartIndexUpdate(){
    if(up) return true;
    if(!OSData::SD_usable) return false;
    void* mem = malloc(sizeof(Updater));
    if(!mem){
        LOG_APP_WARN("ミュージック: 索引を作るためのメモリ(%uB)が足りません", (unsigned)sizeof(Updater));
        return false;
    }
    up = new (mem) Updater();
    memset(up->slot_index, 0, sizeof(up->slot_index));
    if(!OpenIndex(up->old_index, up->old_count)) up->old_count = 0;
    up->out = OSData::SD.open(PICO_Path::FILE::CFG::SYS_MUSIC_INDEX_TMP, O_WRONLY | O_CREAT | O_TRUNC);
    if(!up->out){
        LOG_APP_WARN("ミュージック: 索引を書けません(%s)", PICO_Path::FILE::CFG::SYS_MUSIC_INDEX_TMP);
        FreeUpdater(false);
        return false;
    }
    uint8_t h[kHeaderBytes] = {};
    memcpy(h, kMagic, 4);
    const uint32_t ver = kIndexVersion, rec = sizeof(Record);
    memcpy(h + 4, &ver, 4);
    memcpy(h + 8, &rec, 4);
    up->out.write(h, sizeof(h));
    up->folder_count = ScanFolders(up->folders, kMaxFolders);
    up->cur_folder = -2;    //OpenNextFolder() で -1(直下)から
    return true;
}

bool MusicLibrary::StepIndexUpdate(uint32_t budget_ms){
    if(!up) return true;
    if(!OSData::SD_usable){ CancelIndexUpdate(); return true; }
    const uint32_t start = millis();
    // 1. 前の索引を表へ(1回に64件ずつ)
    if(up->old_loaded < up->old_count){
        if(up->old_loaded == 0) up->old_index.seek(kHeaderBytes);
        Record r;
        for(int n = 0; n < 64 && up->old_loaded < up->old_count; n++){
            if(!NextRecord(up->old_index, r)){ up->old_count = up->old_loaded; break; }
            const uint32_t h = Fnv(r.folder, r.file);
            for(int k = 0; k < Updater::kSlots; k++){
                const int s = (int)((h + (uint32_t)k) % Updater::kSlots);
                if(up->slot_index[s] != 0) continue;
                up->hash[s] = h;
                up->slot_index[s] = (uint16_t)(up->old_loaded + 1);
                break;
            }
            up->old_loaded++;
        }
        return false;
    }
    // 2. フォルダを順に見て記録を書く(少なくとも1曲は進める)
    if(!up->scanning){
        up->scanning = true;
        if(!OpenNextFolder()) goto finish;
    }
    while(true){
        FsFile file;
        if(!file.openNext(&up->dir, O_RDONLY)){
            if(!OpenNextFolder()) goto finish;
            continue;
        }
        char name[128];
        const bool ok = file.getName(name, sizeof(name)) && !file.isDirectory() && IsAudio(name) && strlen(name) < sizeof(Record::file);
        if(ok){
            AddFile(up->cur_folder >= 0 ? up->folders[up->cur_folder].c_str() : "", file, name);
            up->checked++;
        }
        file.close();
        if((uint32_t)(millis() - start) >= budget_ms) return false;
    }
finish:
    {
        up->out.close();
        up->old_index.close();
        const int written = up->written > kMaxIndexed ? kMaxIndexed : up->written;
        FreeUpdater(false);
        OSData::SD.remove(PICO_Path::FILE::CFG::SYS_MUSIC_INDEX);
        if(!OSData::SD.rename(PICO_Path::FILE::CFG::SYS_MUSIC_INDEX_TMP, PICO_Path::FILE::CFG::SYS_MUSIC_INDEX)){
            LOG_APP_WARN("ミュージック: 索引を差し替えられません");
        }else{
            LOG_APP_MSG("ミュージック: 索引を作りました(%d曲)", written);
        }
        revision++;
    }
    return true;
}

void MusicLibrary::CancelIndexUpdate(){ FreeUpdater(true); }
bool MusicLibrary::IndexUpdating(){ return up != nullptr; }
int MusicLibrary::IndexUpdateDone(){ return up ? up->checked : 0; }
uint32_t MusicLibrary::IndexRevision(){ return revision; }
