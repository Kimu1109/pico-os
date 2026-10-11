#include "storage/SD_IO.hpp"

#include "OS_Data.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

bool PICO_IO::removeRecursive(const char* path){
    FsFile dir = OSData::SD.open(path);

    if (!dir) {
        return false;
    }

    if (!dir.isDir()) {
        dir.close();
        return OSData::SD.remove(path);
    }

    FsFile entry;

    while (entry.openNext(&dir, O_RDONLY)) {
        FixedString<PICO_STR_256B> entryName;
        char nameBuf[PICO_STR_256B];
        entry.getName(nameBuf, sizeof(nameBuf));
        entryName.assign(nameBuf);

        FixedString<PICO_PATH_LEN> fullPath;
        // "path/entryName" を組み立てる。PICO_IO::join同様の連結だが、
        // ここではSDライブラリの都合上単純結合で十分(先頭'/'の重複はSD側が許容)。
        fullPath.assign(path);
        fullPath.append("/");
        fullPath.append(entryName);

        if (entry.isDir()) {
            entry.close();

            if (!removeRecursive(fullPath.c_str())) {
                dir.close();
                return false;
            }
        } else {
            entry.close();

            if (!OSData::SD.remove(fullPath.c_str())) {
                dir.close();
                return false;
            }
        }
    }

    dir.close();

    return OSData::SD.rmdir(path);
}

// ---------- コピー ----------
//
// フォルダを辿るたびにパスをスタックへ積むと、実機のコア0のスタック(4KiB)を深い階層で
// 食い潰すので、組み立て中のパスと読み書きの作業場所は静的に1つずつ持ち、
// 子へ降りるときは末尾へ名前を足し、戻ったら元の長さへ切り詰めて使い回す。
// (同時に2つのコピーが走ることは無い。1コア目だけから呼ぶ)
namespace {
    char g_copy_src[PICO_PATH_LEN + 1];
    char g_copy_dst[PICO_PATH_LEN + 1];
    char g_copy_name[PICO_STR_256B];
    uint8_t g_copy_buf[512];
    void (*g_copy_tick)() = nullptr;

    bool AppendSegment(char* buf, size_t len, const char* name, size_t& out_len){
        const size_t nl = strlen(name);
        const bool root = (len == 1 && buf[0] == '/');
        const size_t total = len + (root ? 0 : 1) + nl;
        if(total > PICO_PATH_LEN) return false;
        if(!root) buf[len++] = '/';
        memcpy(buf + len, name, nl);
        buf[total] = '\0';
        out_len = total;
        return true;
    }

    bool CopyFileRaw(const char* src, const char* dst){
        FsFile in = OSData::SD.open(src, O_RDONLY);
        if(!in) return false;
        FsFile out = OSData::SD.open(dst, O_WRITE | O_CREAT | O_TRUNC);
        if(!out){
            in.close();
            return false;
        }
        bool ok = true;
        for(;;){
            const int n = in.read(g_copy_buf, sizeof(g_copy_buf));
            if(n < 0){ ok = false; break; }
            if(n == 0) break;
            if(out.write(g_copy_buf, (size_t)n) != (size_t)n){ ok = false; break; }
            if(g_copy_tick) g_copy_tick();
        }
        out.close();
        in.close();
        if(!ok) OSData::SD.remove(dst);
        return ok;
    }

    // g_copy_src[0..slen) を g_copy_dst[0..dlen) へコピーする
    bool CopyRec(size_t slen, size_t dlen, int depth){
        g_copy_src[slen] = '\0';
        g_copy_dst[dlen] = '\0';

        FsFile dir = OSData::SD.open(g_copy_src);
        if(!dir) return false;
        if(!dir.isDir()){
            dir.close();
            return CopyFileRaw(g_copy_src, g_copy_dst);
        }
        if(depth >= PICO_IO::kMaxCopyDepth || !OSData::SD.mkdir(g_copy_dst)){
            dir.close();
            return false;
        }

        FsFile entry;
        bool ok = true;
        while(ok && entry.openNext(&dir, O_RDONLY)){
            const bool got = entry.getName(g_copy_name, sizeof(g_copy_name));
            entry.close();
            if(!got){ ok = false; break; }

            size_t cs = 0, cd = 0;
            if(!AppendSegment(g_copy_src, slen, g_copy_name, cs) ||
               !AppendSegment(g_copy_dst, dlen, g_copy_name, cd)){
                ok = false;
                break;
            }
            ok = CopyRec(cs, cd, depth + 1);
            //子の分を切り落として、この階層のパスへ戻す
            g_copy_src[slen] = '\0';
            g_copy_dst[dlen] = '\0';
            if(g_copy_tick) g_copy_tick();
        }
        dir.close();
        return ok;
    }
}

bool PICO_IO::copyRecursive(const char* src, const char* dst, void (*tick)()){
    if(!src || !dst) return false;
    const size_t sl = strlen(src), dl = strlen(dst);
    if(sl == 0 || dl == 0 || sl > PICO_PATH_LEN || dl > PICO_PATH_LEN) return false;
    if(isSameOrInside(dst, src)) return false; //自分の中へはコピーできない
    if(OSData::SD.exists(dst)) return false;

    memcpy(g_copy_src, src, sl + 1);
    memcpy(g_copy_dst, dst, dl + 1);
    g_copy_tick = tick;
    const bool ok = CopyRec(sl, dl, 0);
    g_copy_tick = nullptr;

    if(!ok && OSData::SD.exists(dst)) removeRecursive(dst); //途中まで作ったものを片付ける
    return ok;
}

bool PICO_IO::uniquePath(FixedString<PICO_PATH_LEN>& out, const char* dir, const char* name){
    if(!join(out, dir, name)) return false;
    if(!OSData::SD.exists(out.c_str())) return true;

    //拡張子の手前に " (n)" を挟む。先頭の"."(隠しファイル)は拡張子とみなさない
    const char* dot = strrchr(name, '.');
    if(dot == name) dot = nullptr;
    const size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
    const char* ext = dot ? dot : "";

    for(int n = 2; n <= 99; n++){
        char candidate[PICO_STR_256B];
        const int w = snprintf(candidate, sizeof(candidate), "%.*s (%d)%s", (int)base_len, name, n, ext);
        if(w <= 0 || (size_t)w >= sizeof(candidate)) return false;
        if(!join(out, dir, candidate)) return false;
        if(!OSData::SD.exists(out.c_str())) return true;
    }
    return false;
}
