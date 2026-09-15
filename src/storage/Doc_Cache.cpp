#include "storage/Doc_Cache.hpp"

#include "OS_Data.hpp"
#include "storage/SD_IO.hpp"
#include "functions/Log_Functions.hpp"

#include <cstdio>
#include <cstring>

namespace {

    // 一時ファイルの接尾辞。キャッシュ本体と同じディレクトリへ置く
    // (別ディレクトリだとrename()がボリュームを跨ぐ可能性があるため)
    constexpr const char* kTempSuffix = ".part";
    constexpr const char* kIndexTempSuffix = ".tmp";

    // FATで使えない文字。これらとASCII制御文字は '_' へ置き換える
    bool isForbiddenForFat(char c){
        const unsigned char u = (unsigned char)c;
        if(u < 0x20 || u == 0x7F) return true;
        return strchr(":*?<>|\"\\/", c) != nullptr;
    }

    // 行末のCR/LFを落とす
    void chomp(char* line){
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')){
            line[--n] = '\0';
        }
    }

    // 行をタブで分割する。fieldsは行バッファ内を指す(コピーしない)。
    // 見つかったフィールド数を返す(maxFieldsで頭打ち)
    int splitTsv(char* line, char* fields[], int maxFields){
        int count = 0;
        char* cursor = line;

        while(count < maxFields){
            fields[count++] = cursor;
            char* tab = strchr(cursor, '\t');
            if(!tab) break;
            *tab = '\0';
            cursor = tab + 1;
        }
        return count;
    }

    // 目録1行を組み立てて書き出す
    bool writeEntryLine(FsFile& dst, const PICO_DocCache::Entry& entry){
        char line[PICO_DocCache::kMaxIndexLineLen];
        const int n = snprintf(line, sizeof(line), "%s\t%s\t%s\t%lu\t%lu\n",
            entry.host.c_str(), entry.path.c_str(), entry.validator.c_str(),
            (unsigned long)entry.fetched_epoch, (unsigned long)entry.size);

        if(n <= 0 || n >= (int)sizeof(line)) return false;
        return dst.write(line, (size_t)n) == (size_t)n;
    }

    // 行がhost/pathの組と一致するか。一致した場合はフィールドの分割結果も返す
    bool lineMatches(char* line, const char* host, const char* path, char* fields[5], int& fieldCount){
        fieldCount = splitTsv(line, fields, 5);
        if(fieldCount < 2) return false;
        return strcmp(fields[0], host) == 0 && strcmp(fields[1], path) == 0;
    }

    // host/pathを正規化して、目録での照合に使う形にそろえる
    bool canonicalize(const char* host, const char* path,
                      FixedString<PICO_STR_M>& hostOut, FixedString<PICO_PATH_LEN>& pathOut){
        if(!PICO_DocCache::SanitizeHost(hostOut, host)) return false;
        //パスは "/" 始まりへそろえる("." や ".." も畳む)
        return PICO_IO::normalize(pathOut, path ? path : "/");
    }
}

// ---------- ホスト名とパス ----------

bool PICO_DocCache::SanitizeHost(FixedString<PICO_STR_M>& out, const char* host){
    if(!host || host[0] == '\0') return false;

    out.clear();

    for(const char* p = host; *p; p++){
        char c = *p;

        //DNSは大小を区別しないので小文字へそろえる(FATも区別しないため、
        //そろえておかないと同じホストが2つのディレクトリに分かれうる)
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');

        //":" はポート番号で頻出するが、FATのファイル名には使えない
        if(isForbiddenForFat(c)) c = '_';

        char buf[2] = { c, '\0' };
        if(!out.append(buf)) return false; //収まらないなら黙って切らずに失敗させる
    }

    return !out.empty();
}

bool PICO_DocCache::PathFor(FixedString<PICO_PATH_LEN>& out, const char* host, const char* path){
    FixedString<PICO_STR_M> safeHost;
    FixedString<PICO_PATH_LEN> normalized;
    if(!canonicalize(host, path, safeHost, normalized)) return false;

    //ディレクトリそのものを指す参照はキャッシュできない
    if(normalized == FixedString<PICO_PATH_LEN>("/")) return false;

    FixedString<PICO_PATH_LEN> base;
    if(!PICO_IO::join(base, PICO_Path::DIR::CACHE, safeHost.c_str())) return false;

    return PICO_IO::join(out, base.c_str(), normalized.c_str());
}

bool PICO_DocCache::Exists(const char* host, const char* path){
    FixedString<PICO_PATH_LEN> full;
    if(!PathFor(full, host, path)) return false;
    return OSData::SD.exists(full.c_str());
}

// ---------- 目録 ----------

bool PICO_DocCache::Lookup(const char* host, const char* path, Entry& out){
    FixedString<PICO_STR_M> safeHost;
    FixedString<PICO_PATH_LEN> normalized;
    if(!canonicalize(host, path, safeHost, normalized)) return false;

    FsFile src = OSData::SD.open(PICO_Path::FILE::CACHE_INDEX_TSV, O_RDONLY);
    if(!src) return false;

    char line[kMaxIndexLineLen];
    bool found = false;

    while(src.fgets(line, sizeof(line)) > 0){
        chomp(line);
        if(line[0] == '\0' || line[0] == '#') continue;

        char* fields[5];
        int fieldCount = 0;
        if(!lineMatches(line, safeHost.c_str(), normalized.c_str(), fields, fieldCount)) continue;

        out.host.assign(fields[0]);
        out.path.assign(fields[1]);
        out.validator.assign(fieldCount > 2 ? fields[2] : "");
        out.fetched_epoch = (fieldCount > 3) ? (uint32_t)strtoul(fields[3], nullptr, 10) : 0;
        out.size = (fieldCount > 4) ? (uint32_t)strtoul(fields[4], nullptr, 10) : 0;
        found = true;
        break;
    }

    src.close();
    return found;
}

namespace {
    // 目録を書き写しながら、host/pathが一致する行を差し替える/落とす。
    // replacement が nullptr なら削除、そうでなければ差し替え(無ければ末尾へ追加)。
    //
    // 元ファイルを読みつつ一時ファイルへ書き、最後にrenameで差し替える。
    // Config_Functions::SetValue() と同じ手順で、目録全体をRAMへ載せずに済む
    bool rewriteIndex(const char* host, const char* path, const PICO_DocCache::Entry* replacement){
        FixedString<PICO_PATH_LEN> tmpPath;
        if(!tmpPath.assign(PICO_Path::FILE::CACHE_INDEX_TSV)) return false;
        if(!tmpPath.append(kIndexTempSuffix)) return false;

        //目録は /cache 直下なので、まだ無ければ作っておく
        OSData::SD.mkdir(PICO_Path::DIR::CACHE);

        FsFile dst = OSData::SD.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if(!dst){
            LOG_SYS_FAIL("DocCache: 目録の一時ファイルを作成できません (%s)", tmpPath.c_str());
            return false;
        }

        bool ok = true;
        bool replaced = false;

        FsFile src = OSData::SD.open(PICO_Path::FILE::CACHE_INDEX_TSV, O_RDONLY);
        if(src){
            char line[PICO_DocCache::kMaxIndexLineLen];
            char raw[PICO_DocCache::kMaxIndexLineLen];

            while(ok && src.fgets(line, sizeof(line)) > 0){
                //splitTsvは行を書き換えるので、書き写す用の原本を取っておく
                strncpy(raw, line, sizeof(raw) - 1);
                raw[sizeof(raw) - 1] = '\0';

                chomp(line);
                if(line[0] == '\0'){
                    continue; //空行は落とす
                }

                char* fields[5];
                int fieldCount = 0;
                const bool hit = (line[0] != '#') && lineMatches(line, host, path, fields, fieldCount);

                if(hit){
                    //2件目以降の重複行は落とす(Config_Functions::SetValueと同じ扱い)
                    if(!replaced && replacement){
                        ok = writeEntryLine(dst, *replacement);
                        replaced = true;
                    }else if(!replacement){
                        replaced = true;
                    }
                    continue;
                }

                //書き写す。元の行に改行が無かった場合(最終行)は足す
                size_t n = strlen(raw);
                if(n == 0) continue;
                if(raw[n - 1] != '\n'){
                    if(n + 1 >= sizeof(raw)){ ok = false; break; }
                    raw[n] = '\n';
                    raw[n + 1] = '\0';
                    n++;
                }
                ok = (dst.write(raw, n) == n);
            }
            src.close();
        }

        if(ok && replacement && !replaced){
            ok = writeEntryLine(dst, *replacement);
        }

        dst.close();

        if(!ok){
            LOG_SYS_FAIL("DocCache: 目録の書き込みに失敗しました");
            OSData::SD.remove(tmpPath.c_str());
            return false;
        }

        if(OSData::SD.exists(PICO_Path::FILE::CACHE_INDEX_TSV)
            && !OSData::SD.remove(PICO_Path::FILE::CACHE_INDEX_TSV)){
            LOG_SYS_FAIL("DocCache: 古い目録を削除できません");
            OSData::SD.remove(tmpPath.c_str());
            return false;
        }
        if(!OSData::SD.rename(tmpPath.c_str(), PICO_Path::FILE::CACHE_INDEX_TSV)){
            LOG_SYS_FAIL("DocCache: 目録を差し替えられません");
            return false;
        }

        return true;
    }
}

bool PICO_DocCache::SetEntry(const Entry& entry){
    FixedString<PICO_STR_M> safeHost;
    FixedString<PICO_PATH_LEN> normalized;
    if(!canonicalize(entry.host.c_str(), entry.path.c_str(), safeHost, normalized)) return false;

    //目録へ書くのは正規化した形。照合のたびに正規化し直さずに済む
    Entry stored = entry;
    if(!stored.host.assign(safeHost)) return false;
    if(!stored.path.assign(normalized)) return false;

    return rewriteIndex(safeHost.c_str(), normalized.c_str(), &stored);
}

bool PICO_DocCache::RemoveEntry(const char* host, const char* path){
    FixedString<PICO_STR_M> safeHost;
    FixedString<PICO_PATH_LEN> normalized;
    if(!canonicalize(host, path, safeHost, normalized)) return false;

    return rewriteIndex(safeHost.c_str(), normalized.c_str(), nullptr);
}

bool PICO_DocCache::Remove(const char* host, const char* path){
    FixedString<PICO_PATH_LEN> full;
    if(!PathFor(full, host, path)) return false;

    //本体が無くても目録は掃除する(片方だけ残っている状態を解消したいため)
    if(OSData::SD.exists(full.c_str())) OSData::SD.remove(full.c_str());

    return RemoveEntry(host, path);
}

bool PICO_DocCache::Clear(){
    //目録も /cache の下にあるので、まとめて消える
    return PICO_IO::removeRecursive(PICO_Path::DIR::CACHE);
}

// ---------- Writer ----------

bool PICO_DocCache::Writer::begin(const char* host, const char* path, uint32_t max_bytes){
    this->abort(); //前回の書きかけが残っていたら捨てる

    FixedString<PICO_STR_M> safeHost;
    FixedString<PICO_PATH_LEN> normalized;
    if(!canonicalize(host, path, safeHost, normalized)) return false;
    if(!PathFor(final_path, host, path)) return false;

    if(!host_.assign(safeHost)) return false;
    if(!path_.assign(normalized)) return false;

    if(!temp_path.assign(final_path)) return false;
    if(!temp_path.append(kTempSuffix)) return false;

    //キャッシュはサーバ上のパスをミラーするので、途中のディレクトリを作る
    //(SdFat::mkdirは既定で親も作る)
    char dir[PICO_PATH_LEN];
    if(PICO_IO::parent(dir, final_path.c_str())){
        OSData::SD.mkdir(dir);
    }

    file = OSData::SD.open(temp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if(!file){
        LOG_SYS_FAIL("DocCache: 一時ファイルを作成できません (%s)", temp_path.c_str());
        return false;
    }

    written_ = 0;
    max_bytes_ = (max_bytes > 0) ? max_bytes : kMaxEntryBytes;
    open_ = true;
    failed_ = false;
    return true;
}

bool PICO_DocCache::Writer::write(const void* data, size_t len){
    if(!open_ || failed_) return false;
    if(len == 0) return true;
    if(!data){
        failed_ = true;
        return false;
    }

    //相手のサーバが何を返してきてもSDを埋め尽くさないよう、ここで頭打ちにする
    if(written_ + (uint32_t)len > max_bytes_){
        LOG_SYS_WARN("DocCache: %s が上限(%luB)を超えたため打ち切りました",
            path_.c_str(), (unsigned long)max_bytes_);
        failed_ = true;
        return false;
    }

    if(file.write(data, len) != len){
        LOG_SYS_FAIL("DocCache: 書き込みに失敗しました (%s)", temp_path.c_str());
        failed_ = true;
        return false;
    }

    written_ += (uint32_t)len;
    return true;
}

bool PICO_DocCache::Writer::commit(const char* validator, uint32_t fetched_epoch){
    if(!open_) return false;

    file.close();
    open_ = false;

    //途中で失敗していたら半端なファイルを残さない。
    //ここで差し替えてしまうと「正常なキャッシュ」として次回読まれてしまう
    if(failed_){
        OSData::SD.remove(temp_path.c_str());
        return false;
    }

    if(OSData::SD.exists(final_path.c_str()) && !OSData::SD.remove(final_path.c_str())){
        LOG_SYS_FAIL("DocCache: 古い本体を削除できません (%s)", final_path.c_str());
        OSData::SD.remove(temp_path.c_str());
        return false;
    }
    if(!OSData::SD.rename(temp_path.c_str(), final_path.c_str())){
        LOG_SYS_FAIL("DocCache: 本体を差し替えられません (%s)", final_path.c_str());
        OSData::SD.remove(temp_path.c_str());
        return false;
    }

    Entry entry;
    entry.host.assign(host_);
    entry.path.assign(path_);
    //validatorが無いサーバもある(その場合は空のまま=毎回取り直すことになる)
    entry.validator.assign(validator ? validator : "");
    entry.fetched_epoch = fetched_epoch;
    entry.size = written_;

    if(!SetEntry(entry)){
        //本体は差し替わっているので、目録だけ失敗した状態。
        //次回のLookupが空振りして取り直しになるだけなので、警告に留める
        LOG_SYS_WARN("DocCache: 目録を更新できませんでした (%s)", path_.c_str());
    }

    return true;
}

void PICO_DocCache::Writer::abort(){
    if(!open_) return;

    file.close();
    open_ = false;
    OSData::SD.remove(temp_path.c_str());
    written_ = 0;
    failed_ = false;
}
