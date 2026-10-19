// pvid_test.py の下請け。.pvid を本体の読み取り(PvidPlayer)で解き、フレームごとの絵のハッシュを書き出す。
//
//   pvid_dump file.pvid             … 頭から最後まで
//   pvid_dump file.pvid seek N [M…] … N 番目(続けて M 番目…)へ飛んだ絵
//
// 出力(1行1件):
//   open <幅> <高さ> <fps分子> <fps分母> <フレーム数> <長さms>   … 開けたとき
//   error <どこで> <理由の番号>                                  … 開けない/壊れていたとき(ここで終わる)
//   frame <番号> <ハッシュ> <変わったタイルの行の数>
//   end
// ハッシュは今の絵(1行 幅/2 バイト、左の画素が上位4bit)の FNV-1a 64bit。
#include "apps/video/Pvid_Player.hpp"
#include "OS_Data.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {
    uint64_t Hash(const PvidPlayer& p){
        uint64_t h = 1469598103934665603ULL;
        const uint8_t* s = p.screen();
        const size_t n = (size_t)p.stride() * p.height();
        for(size_t i = 0; i < n; i++){ h ^= s[i]; h *= 1099511628211ULL; }
        return h;
    }
    int DirtyRows(PvidPlayer& p){
        int n = 0;
        p.takeDirty([&](int, int, int){ n++; });
        return n;
    }
}

int main(int argc, char** argv){
    if(argc < 2){ fprintf(stderr, "usage: pvid_dump file.pvid [seek N...]\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    HostSd::files["/v.pvid"] = ss.str();
    OSData::SD_usable = true;

    PvidPlayer p;
    const PvidPlayer::Error e = p.open("/v.pvid");
    if(e != PvidPlayer::Error::None){ printf("error open %d\n", (int)e); return 0; }
    printf("open %d %d %u %u %" PRIu32 " %" PRIu32 "\n", p.width(), p.height(), p.fpsNum(), p.fpsDen(),
           p.frameCount(), p.durationMs());
    DirtyRows(p);

    if(argc >= 4 && std::string(argv[2]) == "seek"){
        for(int i = 3; i < argc; i++){
            const uint32_t n = (uint32_t)strtoul(argv[i], nullptr, 10);
            const PvidPlayer::Error se = p.seekTo(n);
            if(se != PvidPlayer::Error::None){ printf("error seek %d\n", (int)se); return 0; }
            printf("frame %" PRId32 " %016" PRIx64 " %d\n", p.current(), Hash(p), DirtyRows(p));
        }
        printf("end\n");
        return 0;
    }
    for(;;){
        const PvidPlayer::Error de = p.decodeNext();
        if(de == PvidPlayer::Error::Ended) break;
        if(de != PvidPlayer::Error::None){ printf("error frame %d\n", (int)de); return 0; }
        printf("frame %" PRId32 " %016" PRIx64 " %d\n", p.current(), Hash(p), DirtyRows(p));
    }
    // 終わった後は Ended のまま
    printf(p.decodeNext() == PvidPlayer::Error::Ended ? "end\n" : "error after-end 0\n");
    return 0;
}
