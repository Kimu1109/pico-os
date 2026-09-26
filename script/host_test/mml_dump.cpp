// midi2mml_test.py の下請け。MMLを本物の読み取り(MmlCompiler)へ通し、演奏データを1行ずつ書き出す。
//
//   mml_dump file.mml
//
// 出力(1行1件、空白区切り):
//   result ok|ng <行> <列> <バイト数> <テンポ>     … 1行目。ngなら次の行に理由
//   message <理由> / warning <警告>                 … あれば
//   note <ch> <ティック> <ノート番号> <長さ>
//   rest <ch> <ティック> <長さ>
//   tempo <ch> <ティック> <bpm>
//   wave|volume|env|gate <ch> <ティック> <値>
//   segno <ch> <ティック>
// 繰り返し([ ]n)は展開して、実際に鳴る順に書く(Lは1周目だけ)。
#include "sound/Mml_Compiler.hpp"
#include "sound/Music_Data.hpp"
#include "functions/Log_Functions.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}

namespace {
    void DumpChannel(const uint8_t* d, uint16_t size, int ch, uint16_t start){
        using namespace MusicData;
        struct Frame { uint16_t body; int left; };
        Frame stack[kMaxLoopDepth];
        int depth = 0;
        long tick = 0;
        uint16_t p = start;
        for(int guard = 0; guard < 1000000 && p < size; guard++){
            const uint8_t op = d[p++];
            switch(op){
                case End: return;
                case Note:
                    printf("note %d %ld %d %d\n", ch, tick, d[p], ReadU16(d + p + 1));
                    tick += ReadU16(d + p + 1); p += 3; break;
                case Rest:
                    printf("rest %d %ld %d\n", ch, tick, ReadU16(d + p));
                    tick += ReadU16(d + p); p += 2; break;
                case Wave:     printf("wave %d %ld %d\n", ch, tick, d[p]); p += 1; break;
                case Volume:   printf("volume %d %ld %d\n", ch, tick, d[p]); p += 1; break;
                case Envelope: printf("env %d %ld %d\n", ch, tick, (int8_t)d[p]); p += 1; break;
                case Gate:     printf("gate %d %ld %d\n", ch, tick, d[p]); p += 1; break;
                case Tempo:    printf("tempo %d %ld %d\n", ch, tick, ReadU16(d + p)); p += 2; break;
                case Segno:    printf("segno %d %ld\n", ch, tick); break;
                case SaveState: case RestoreState: break;
                case LoopBegin:
                    stack[depth].left = d[p]; p += 1;
                    stack[depth].body = p;
                    depth++;
                    break;
                case LoopBreak:
                    if(stack[depth - 1].left == 1){ p = ReadU16(d + p); depth--; }
                    else p += 2;
                    break;
                case LoopEnd:
                    if(--stack[depth - 1].left > 0) p = stack[depth - 1].body;
                    else depth--;
                    break;
                default:
                    printf("bad %d %d\n", ch, op);
                    return;
            }
        }
    }
}

int main(int argc, char** argv){
    if(argc < 2){ fprintf(stderr, "usage: mml_dump file.mml\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if(!f){ fprintf(stderr, "開けません: %s\n", argv[1]); return 2; }
    std::stringstream ss; ss << f.rdbuf();
    const std::string text = ss.str();

    static uint8_t buf[6144];
    MmlCompiler c;
    MmlResult r;
    MmlTextSource src(text.data(), text.size());
    const bool ok = c.compile(src, buf, sizeof buf, r);
    printf("result %s %d %d %u %u\n", ok ? "ok" : "ng", r.line, r.col, (unsigned)r.size,
           ok ? (unsigned)MusicData::ReadU16(buf + 4) : 0u);
    if(r.message.length()) printf("message %s\n", r.message.c_str());
    if(r.warning.length()) printf("warning %s\n", r.warning.c_str());
    if(!ok) return 0;
    for(int ch = 0; ch < MusicData::kChannels; ch++){
        const uint16_t start = MusicData::ReadU16(buf + 8 + ch * 2);
        if(start) DumpChannel(buf, r.size, ch, start);
    }
    return 0;
}
