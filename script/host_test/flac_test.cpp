// FLACの再生の検証。
//
// 前半は FlacDecoder(見出し・ビット単位の一致・左右・周波数の変換・シーク・ループ・断り方・壊れたファイル)。
// .flac は data/flac/*.flac(gen_flac_test_data.py が ffmpeg で作ったもの)を stubs/SdFat.h の HostSd::files へ置く。
// 正解は ffmpeg の展開結果のハッシュ(flac_test_data.hpp)。**元の周波数のまま読めばビット単位で一致する**。
//
// 後半は SoundFunctions の配線(WavPlay が .flac を FlacDecoder で読む・MP3と行き来できる・止めると返す)と、
// ミュージックの索引が使うタグの読み取り(VORBIS_COMMENT)。
#include "hal/Sound_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Flac_Decoder.hpp"
#include "sound/Audio_Tags.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"
#include "flac_test_data.hpp"
#include "mp3_test_data.hpp"
#include "apps/music/Music_Service.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// data/flac/<名前>.flac を読む(このファイルの隣)
static std::string Load(const char* name){
    std::string here = __FILE__;
    here = here.substr(0, here.find_last_of('/') + 1) + "data/flac/" + name + ".flac";
    std::ifstream in(here, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void Put(const char* path, const char* name){ HostSd::files[path] = Load(name); }

// 終わりまで読む(lanes が2なら左右交互)。limit フレームで打ち切る
static std::vector<int16_t> ReadAll(FlacDecoder& dec, size_t limit = 2000000){
    std::vector<int16_t> all;
    PcmSample buf[512];   //24bitの版(flac24_test)では32bitの入れ物(16bitの桁)
    const size_t lanes = dec.outChannels();
    size_t frames = 0;
    while(!dec.finished() && frames < limit){
        const size_t n = dec.read(buf, limit - frames < 256 ? limit - frames : 256);
        all.insert(all.end(), buf, buf + n * lanes);
        frames += n;
        if(n == 0) break;
    }
    return all;
}

static unsigned long long Fnv(const std::vector<int16_t>& v){
    unsigned long long h = 0xCBF29CE484222325ULL;
    for(int16_t s : v) h = (h ^ (unsigned long long)(uint16_t)s) * 0x100000001B3ULL;
    return h;
}

// 周波数と振幅(上向きのゼロ交差の数・RMS)
static double Freq(const std::vector<int16_t>& v, size_t lanes, size_t lane, size_t from, size_t to, uint32_t rate){
    int ups = 0;
    for(size_t i = from + 1; i < to; i++){
        if(v[(i - 1) * lanes + lane] < 0 && v[i * lanes + lane] >= 0) ups++;
    }
    return (double)ups * rate / (double)(to - from);
}

// STREAMINFO(ファイル先頭から8バイト目〜)の周波数・チャンネル数・ビット数・最大ブロック長を書き換える
static std::string Patch(std::string f, uint32_t rate, int ch, int bps, int max_block = -1){
    unsigned char* p = (unsigned char*)&f[0];
    p[18] = (unsigned char)(rate >> 12);
    p[19] = (unsigned char)(rate >> 4);
    p[20] = (unsigned char)(((rate & 15) << 4) | (((ch - 1) & 7) << 1) | (((bps - 1) >> 4) & 1));
    p[21] = (unsigned char)((p[21] & 0x0F) | (((bps - 1) & 15) << 4));
    if(max_block >= 0){ p[10] = (unsigned char)(max_block >> 8); p[11] = (unsigned char)max_block; }
    return f;
}

struct Case { const char* name; unsigned frames; unsigned long long hash; int channels; int bps; uint32_t rate; };

static void TestBitExact(){
    OSData::SD_usable = true;
    const Case cases[] = {
        {"kFlacStereo16",      kFlacStereo16Frames,      kFlacStereo16Hash,      2, 16, 44100},
        {"kFlacStereo16Fixed", kFlacStereo16FixedFrames, kFlacStereo16FixedHash, 2, 16, 44100},
        {"kFlacMono200",       kFlacMono200Frames,       kFlacMono200Hash,       1, 16, 22050},
        {"kFlacMono1000",      kFlacMono1000Frames,      kFlacMono1000Hash,      1, 16, 22050},
        {"kFlacStereo24",      kFlacStereo24Frames,      kFlacStereo24Hash,      2, 24, 48000},
        {"kFlacStereo24At96k", kFlacStereo24At96kFrames, kFlacStereo24At96kHash, 2, 24, 96000},
        {"kFlacSilence",       kFlacSilenceFrames,       kFlacSilenceHash,       2, 16, 44100},
        {"kFlacNoise",         kFlacNoiseFrames,         kFlacNoiseHash,         2, 16, 44100},
        {"kFlacChirp",         kFlacChirpFrames,         kFlacChirpHash,         1, 16, 22050},
        {"kFlacChirpSeekTable",kFlacChirpFrames,         kFlacChirpHash,         1, 16, 22050},
    };
    printf("--- ffmpegの展開結果とビット単位で一致 ---\n");
    for(const Case& c : cases){
        std::string path = std::string("/") + c.name + ".flac";
        Put(path.c_str(), c.name);
        FlacDecoder dec;
        dec.setStereoOutput(true);
        char label[160];
        snprintf(label, sizeof(label), "%s: 開けて見出しが合う(%dch・%dbit・%uHz)", c.name, c.channels, c.bps, (unsigned)c.rate);
        check(dec.open(path.c_str(), c.rate) && dec.channels() == c.channels && dec.bitsPerSample() == c.bps && dec.sampleRate() == c.rate
              && dec.outChannels() == c.channels, label);
        const auto all = ReadAll(dec);
        snprintf(label, sizeof(label), "%s: サンプル数 %zu / 期待 %u", c.name, all.size() / c.channels, c.frames);
        check(all.size() == (size_t)c.frames * c.channels && dec.finished(), label);
        snprintf(label, sizeof(label), "%s: 全サンプルが一致(ハッシュ %016llX)", c.name, Fnv(all));
        check(Fnv(all) == c.hash, label);
        const uint32_t want_ms = (uint32_t)((uint64_t)c.frames * 1000 / c.rate);
        snprintf(label, sizeof(label), "%s: 長さ %ums", c.name, (unsigned)dec.durationMs());
        check(dec.durationMs() == want_ms, label);
    }
}

static void TestOutputs(){
    printf("--- 出力の形(モノラル・周波数の変換) ---\n");
    OSData::SD_usable = true;
    Put("/st.flac", "kFlacStereo16");
    std::vector<int16_t> stereo;
    {
        FlacDecoder dec;
        dec.setStereoOutput(true);
        dec.open("/st.flac", 44100);
        stereo = ReadAll(dec);
    }
    {
        FlacDecoder dec;
        check(dec.open("/st.flac", 44100) && dec.outChannels() == 1, "既定はモノラル出力");
        const auto mono = ReadAll(dec);
        bool same = mono.size() * 2 == stereo.size();
        for(size_t i = 0; same && i < mono.size(); i++){
            same = mono[i] == (int16_t)(((int32_t)stereo[2 * i] + stereo[2 * i + 1]) / 2);
        }
        check(same, "モノラルは左右の平均");
    }
    {
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/st.flac", 22050) && dec.outChannels() == 2, "22050Hzへ下げて開ける");
        const auto all = ReadAll(dec);
        const size_t frames = all.size() / 2;
        printf("       %zuフレーム\n", frames);
        check(frames >= 6600 && frames <= 6630, "フレーム数は半分");
        check(dec.finished(), "終わりまで読める");
    }
    {
        //22.05kHzの1kHzを44.1kHzへ上げても1kHz(周波数の変換が元の周波数を正しく扱う)
        Put("/m1000.flac", "kFlacMono1000");
        FlacDecoder dec;
        check(dec.open("/m1000.flac", 44100) && dec.outChannels() == 1, "モノラルを44.1kHzへ上げて開ける");
        const auto all = ReadAll(dec);
        printf("       %zuフレーム %.1fHz\n", all.size(), Freq(all, 1, 0, 200, all.size() - 200, 44100));
        check(all.size() >= 8815 && all.size() <= 8825 && fabs(Freq(all, 1, 0, 200, all.size() - 200, 44100) - 1000) < 15, "フレーム数は倍・音は1kHzのまま");
    }
    {
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/st.flac", 48000), "44.1kHzを48kHzへ上げて開ける");
        const auto all = ReadAll(dec);
        const size_t frames = all.size() / 2;
        check(frames >= 14390 && frames <= 14410, "フレーム数は48/44.1倍");
    }
}

static double RmsLane(const std::vector<int16_t>& v, size_t lanes, size_t lane, size_t from, size_t to){
    double sum = 0;
    for(size_t i = from; i < to; i++){ const double x = v[i * lanes + lane]; sum += x * x; }
    return to > from ? sqrt(sum / (double)(to - from)) : 0;
}

// 96kHz(ハイレゾ)を44.1k/48kへ下げて鳴らす。数と大きさが元の周波数のまま読んだものと合う(低域通過フィルタが聞こえる帯域を削らない)
static void TestHiRes(){
    printf("--- 96kHzを出力の周波数へ下げる ---\n");
    OSData::SD_usable = true;
    Put("/hi96.flac", "kFlacStereo24At96k");
    std::vector<int16_t> native;
    {
        FlacDecoder dec;
        dec.setStereoOutput(true);
        dec.open("/hi96.flac", 96000);
        native = ReadAll(dec);
    }
    const size_t nf = native.size() / 2;
    const double rms_l = RmsLane(native, 2, 0, nf / 5, nf * 4 / 5), rms_r = RmsLane(native, 2, 1, nf / 5, nf * 4 / 5);
    const uint32_t outs[] = {44100, 48000, 22050};
    for(uint32_t out : outs){
        FlacDecoder dec;
        dec.setStereoOutput(true);
        char label[160];
        snprintf(label, sizeof(label), "96kHzを%uHzへ下げて開ける", (unsigned)out);
        check(dec.open("/hi96.flac", out) && dec.outChannels() == 2 && dec.sampleRate() == 96000, label);
        const auto all = ReadAll(dec);
        const size_t frames = all.size() / 2;
        const size_t want = (size_t)((uint64_t)kFlacStereo24At96kFrames * out / 96000);
        snprintf(label, sizeof(label), "  %uHz: フレーム数 %zu / 期待 %zu", (unsigned)out, frames, want);
        check(dec.finished() && frames + 3 >= want && frames <= want + 3, label);
        const double l = RmsLane(all, 2, 0, frames / 5, frames * 4 / 5), r = RmsLane(all, 2, 1, frames / 5, frames * 4 / 5);
        snprintf(label, sizeof(label), "  %uHz: 大きさは元のまま(左 %.0f/%.0f 右 %.0f/%.0f)", (unsigned)out, l, rms_l, r, rms_r);
        check(fabs(l - rms_l) < rms_l * 0.03 && fabs(r - rms_r) < rms_r * 0.03, label);
    }
    {
        //シークも元の周波数で数える(50msの位置は元の4800サンプル目)
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/hi96.flac", 48000) && dec.durationMs() == 100, "96kHz: 長さ100ms");
        check(dec.seekMs(50), "96kHz: 50msへ飛べる");
        const auto rest = ReadAll(dec);
        const size_t frames = rest.size() / 2;
        char label[96];
        snprintf(label, sizeof(label), "96kHz: 飛んだ先から残り %zu フレーム(48kHzで約2400)", frames);
        check(frames + 40 >= 2400 && frames <= 2400 + 40, label);
    }
}

static void TestContainer(){
    printf("--- ID3v2・後ろの余分なデータ・詰め物 ---\n");
    OSData::SD_usable = true;
    const std::string base = Load("kFlacStereo16");
    {
        //ffmpeg は詰め物(PADDING)とタグ(VORBIS_COMMENT)を付けるので、これを読み飛ばして鳴る
        FlacDecoder dec;
        dec.setStereoOutput(true);
        HostSd::files["/base.flac"] = base;
        check(dec.open("/base.flac", 44100), "詰め物・タグ付きでも開ける");
        check(Fnv(ReadAll(dec)) == kFlacStereo16Hash, "読み飛ばしても音は同じ");
    }
    {
        std::string id3 = std::string("ID3\x03\x00\x00", 6) + std::string("\x00\x00\x00\x40", 4) + std::string(64, 'x');
        HostSd::files["/id3.flac"] = id3 + base;
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/id3.flac", 44100), "頭にID3v2があっても開ける");
        check(Fnv(ReadAll(dec)) == kFlacStereo16Hash, "ID3v2の後ろから同じ音");
    }
    {
        std::string junk = "TAG" + std::string(125, 'J') + std::string("\xFF\xF8\xFF\xF8\xFF\xF9", 6) + std::string(200, '\xFF');
        HostSd::files["/junk.flac"] = base + junk;
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/junk.flac", 44100), "後ろに余分なデータがあっても開ける");
        const auto all = ReadAll(dec);
        check(all.size() == kFlacStereo16Frames * 2 && Fnv(all) == kFlacStereo16Hash, "総サンプル数で終わり、余分なデータは鳴らさない");
    }
}

static void TestSeek(){
    printf("--- シーク(SEEKTABLEなし=二分探索 / あり=表) ---\n");
    OSData::SD_usable = true;
    const char* names[2] = {"kFlacChirp", "kFlacChirpSeekTable"};
    for(int k = 0; k < 2; k++){
        std::string path = std::string("/") + names[k] + ".flac";
        Put(path.c_str(), names[k]);
        FlacDecoder full;
        full.open(path.c_str(), 22050);
        const auto ref = ReadAll(full);
        check(full.hasSeekTable() == (k == 1), k ? "SEEKTABLEを読む" : "SEEKTABLEが無い");

        FlacDecoder dec;
        check(dec.open(path.c_str(), 22050), "開ける");
        const uint32_t points[] = {0, 1, 100, 333, 500, 777, 999, 5000};
        for(uint32_t ms : points){
            check(dec.seekMs(ms), "飛べる");
            const auto got = ReadAll(dec);
            size_t at = (size_t)((uint64_t)ms * 22050 / 1000);
            if(at >= ref.size()) at = ref.size() - 1;      //終わりを越えたら最後の1サンプル
            const bool ok = got.size() == ref.size() - at && memcmp(got.data(), ref.data() + at, got.size() * sizeof(int16_t)) == 0;
            char label[96];
            snprintf(label, sizeof(label), "%s: %ums へ飛んだ後はサンプル %zu から同じ音(%zu個)", names[k], (unsigned)ms, at, got.size());
            check(ok, label);
        }
        //途中まで読んだ後に前へ・後ろへ
        dec.seekMs(800);
        PcmSample tmp[300];
        dec.read(tmp, 300);
        dec.seekMs(200);
        const auto back = ReadAll(dec);
        check(back.size() == ref.size() - 4410 && memcmp(back.data(), ref.data() + 4410, back.size() * 2) == 0, "読みかけから前へ戻れる");
        //読み終えて閉じた後でも飛べる(開き直す)
        dec.close();
        check(dec.seekMs(500) && dec.isOpen(), "閉じた後でも開き直して飛べる");
        const auto re = ReadAll(dec);
        check(re.size() == ref.size() - 11025 && memcmp(re.data(), ref.data() + 11025, re.size() * 2) == 0, "開き直した後も同じ音");
    }
}

static void TestLoop(){
    printf("--- ループ ---\n");
    OSData::SD_usable = true;
    Put("/m.flac", "kFlacMono200");
    FlacDecoder dec;
    dec.setLoop(true);
    check(dec.open("/m.flac", 22050), "ループで開ける");
    const auto two = ReadAll(dec, kFlacMono200Frames * 2 + 100);
    check(!dec.finished() && two.size() == kFlacMono200Frames * 2 + 100, "終わりまで来ても終わらない");
    bool same = true;
    for(size_t i = 0; i < kFlacMono200Frames; i++) same = same && two[i] == two[kFlacMono200Frames + i];
    check(same, "2周目は頭から同じ音(継ぎ目に余分な無音が無い)");
    check(two[kFlacMono200Frames + 50] == two[50], "継ぎ目のすぐ後");
}

static void TestErrors(){
    printf("--- 断り方 ---\n");
    OSData::SD_usable = true;
    const std::string base = Load("kFlacStereo16");
    FlacDecoder dec;
    HostSd::files["/bad.flac"] = "hello, this is not a flac file";
    check(!dec.open("/bad.flac", 22050) && dec.error() == FlacDecoder::Error::NotFlac, "FLACでなければNotFlac");
    HostSd::files["/empty.flac"] = "";
    check(!dec.open("/empty.flac", 22050) && dec.error() == FlacDecoder::Error::NotFlac, "空はNotFlac");
    check(!dec.open("/nothing.flac", 22050) && dec.error() == FlacDecoder::Error::OpenFailed, "無いファイルはOpenFailed");
    {
        HostSd::files["/hi.flac"] = Patch(base, 192000, 2, 16);
        check(!dec.open("/hi.flac", 22050) && dec.error() == FlacDecoder::Error::Unsupported, "192kHzはUnsupported");
        HostSd::files["/6ch.flac"] = Patch(base, 44100, 6, 16);
        check(!dec.open("/6ch.flac", 22050) && dec.error() == FlacDecoder::Error::Unsupported, "6chはUnsupported");
        HostSd::files["/32b.flac"] = Patch(base, 44100, 2, 32);
        check(!dec.open("/32b.flac", 22050) && dec.error() == FlacDecoder::Error::Unsupported, "32bitはUnsupported");
        HostSd::files["/blk.flac"] = Patch(base, 44100, 2, 16, 16384);
        check(!dec.open("/blk.flac", 22050) && dec.error() == FlacDecoder::Error::Unsupported, "ブロック長16384はUnsupported");
        printf("       理由の文: %s\n", FlacDecoder::ErrorText(FlacDecoder::Error::Unsupported));
    }
    {
        //メタデータだけで音が無い
        HostSd::files["/head.flac"] = Load("kFlacMono1000").substr(0, 4 + 4 + 34);     //"fLaC" + STREAMINFO だけ
        check(!dec.open("/head.flac", 22050) && dec.error() == FlacDecoder::Error::NoData, "音が無ければNoData");
    }
    OSData::SD_usable = false;
    check(!dec.open("/st.flac", 22050) && dec.error() == FlacDecoder::Error::NoSd, "SDが無ければNoSd");
    OSData::SD_usable = true;
}

static void TestDamaged(){
    printf("--- 壊れたファイル ---\n");
    OSData::SD_usable = true;
    const std::string base = Load("kFlacStereo16");
    {
        HostSd::files["/cut.flac"] = base.substr(0, base.size() * 2 / 3);
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/cut.flac", 44100), "途中で切れていても開ける");
        const auto all = ReadAll(dec);
        check(dec.finished() && all.size() > 0 && all.size() < kFlacStereo16Frames * 2, "読めた所まで鳴らして終わる");
        //読めた所は元の音と同じ(壊れた最後のフレームは出さない)
        FlacDecoder full;
        full.setStereoOutput(true);
        HostSd::files["/ok.flac"] = base;
        full.open("/ok.flac", 44100);
        const auto ref = ReadAll(full);
        check(memcmp(all.data(), ref.data(), all.size() * 2) == 0, "切れる手前までは同じ音");
    }
    {
        //途中のフレームの中身を壊す: そのフレームを捨てて次のフレームから続く(後ろは元の音)
        std::string f = base;
        const size_t mid = f.size() / 2;
        for(size_t i = 0; i < 40; i++) f[mid + i] = (char)(f[mid + i] ^ 0x5A);
        HostSd::files["/dmg.flac"] = f;
        FlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/dmg.flac", 44100), "中が壊れていても開ける");
        const auto all = ReadAll(dec);
        check(dec.finished() && all.size() >= 4096 * 2 && all.size() <= kFlacStereo16Frames * 2, "壊れた所を飛ばして最後まで進む");
        FlacDecoder full;
        full.setStereoOutput(true);
        HostSd::files["/ok.flac"] = base;
        full.open("/ok.flac", 44100);
        const auto ref = ReadAll(full);
        const size_t tail = 1024;
        check(all.size() > tail * 2 && memcmp(all.data() + all.size() - tail * 2, ref.data() + ref.size() - tail * 2, tail * 4) == 0,
              "最後のフレームは壊れていない音のまま");
    }
    {
        //ランダムに壊した60本: クラッシュ・無限ループ・メモリ違反(ASan/UBSan)が無いこと
        uint32_t seed = 12345;
        auto rnd = [&]{ seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const char* names[] = {"kFlacStereo16", "kFlacStereo24", "kFlacNoise", "kFlacMono200"};
        int opened = 0;
        for(int t = 0; t < 60; t++){
            std::string f = Load(names[t % 4]);
            const int hits = 1 + (int)(rnd() % 8);
            for(int h = 0; h < hits; h++) f[(rnd() % f.size())] = (char)rnd();
            if(t % 5 == 0) f.resize(f.size() - rnd() % (f.size() / 2));
            HostSd::files["/fuzz.flac"] = f;
            FlacDecoder d;
            d.setStereoOutput(true);
            if(!d.open("/fuzz.flac", (t & 1) ? 44100 : 22050)) continue;
            opened++;
            ReadAll(d, 200000);
            d.seekMs(rnd() % 500);
            ReadAll(d, 200000);
        }
        printf("       壊した60本のうち %d 本は開けて、最後まで読めた\n", opened);
        check(opened > 0, "壊したファイル60本を読み切れる(落ちない・止まらない)");
    }
}

// ================================================================
// タグ(VORBIS_COMMENT)
// ================================================================

static void LE32(std::string& s, uint32_t v){ for(int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xFF); }
static std::string Block(uint8_t type, bool last, const std::string& body){
    std::string b;
    b += (char)(type | (last ? 0x80 : 0));
    b += (char)((body.size() >> 16) & 0xFF); b += (char)((body.size() >> 8) & 0xFF); b += (char)(body.size() & 0xFF);
    return b + body;
}
static std::string VorbisBody(const std::vector<std::string>& comments){
    std::string b;
    const std::string vendor = "pico-os test";
    LE32(b, (uint32_t)vendor.size()); b += vendor;
    LE32(b, (uint32_t)comments.size());
    for(const std::string& c : comments){ LE32(b, (uint32_t)c.size()); b += c; }
    return b;
}
// kFlacMono1000 の STREAMINFO と音の間に、指定のブロックを挟む
static std::string WithBlocks(const std::vector<std::string>& blocks){
    std::string base = Load("kFlacMono1000");
    std::string si = base.substr(4, 4 + 34);
    si[0] = (char)(si[0] & 0x7F);       //STREAMINFO は最後ではなくなる
    std::string out = "fLaC" + si;
    for(const std::string& b : blocks) out += b;
    return out + base.substr(4 + 4 + 34);
}

static void TestTags(){
    printf("--- タグ(VORBIS_COMMENT) ---\n");
    OSData::SD_usable = true;
    AudioTags t;
    {
        //アルバムアート(PICTURE、3000バイト)を挟んでも飛ばして読む。キーは大小を区別しない
        const std::string pic = Block(6, false, std::string(3000, 'P'));
        const std::string vc = Block(4, true, VorbisBody({"title=\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88\xE6\x9B\xB2", "ARTIST=Some Band", "Album=First Album", "TRACKNUMBER=3/12", "DATE=2026"}));
        HostSd::files["/music/tag.flac"] = WithBlocks({pic, vc});
        check(AudioTagReader::Read("/music/tag.flac", t), "FLACのタグを読める");
        check(strcmp(t.title.c_str(), "テスト曲") == 0, "曲名(UTF-8、キーは小文字でも)");
        check(strcmp(t.artist.c_str(), "Some Band") == 0 && strcmp(t.album.c_str(), "First Album") == 0 && t.track == 3, "アーティスト・アルバム・曲番号");
        FlacDecoder dec;
        check(dec.open("/music/tag.flac", 22050), "タグ・アルバムアート付きでも鳴らせる");
    }
    {
        HostSd::files["/music/notag.flac"] = Load("kFlacStereo16");      //ffmpegの「encoder=…」だけ
        check(!AudioTagReader::Read("/music/notag.flac", t), "知らないキーだけなら false");
        HostSd::files["/music/Upper.FLAC"] = WithBlocks({Block(4, true, VorbisBody({"TITLE=Upper"}))});
        check(AudioTagReader::Read("/music/Upper.FLAC", t) && strcmp(t.title.c_str(), "Upper") == 0, "拡張子の大小は区別しない");
        //壊れたコメント(長さが大きすぎる)でも落ちない
        std::string bad = VorbisBody({"TITLE=Ok"});
        bad[bad.size() - 9] = (char)0x7F;
        HostSd::files["/music/bad.flac"] = WithBlocks({Block(4, true, bad)});
        AudioTagReader::Read("/music/bad.flac", t);
        check(true, "壊れたコメントでも落ちない");
        std::string cut = WithBlocks({Block(4, true, VorbisBody({"TITLE=Cut", "ARTIST=Lost"}))});
        HostSd::files["/music/cut.flac"] = cut.substr(0, 4 + 38 + 4 + 20);
        AudioTagReader::Read("/music/cut.flac", t);
        check(true, "途中で切れていても落ちない");
    }
    check(MusicService::IsPcmFile("a.flac") && MusicService::IsPcmFile("A.FLAC") && MusicService::IsPcmFile("b.mp3")
          && MusicService::IsPcmFile("c.wav") && !MusicService::IsPcmFile("d.mml") && !MusicService::IsPcmFile("flac"), "ミュージックは .flac を鳴らすファイルとして扱う");
}

static void TestMemory(){
    printf("--- 大きさ ---\n");
    printf("       FlacDecoder %zuB\n", sizeof(FlacDecoder));
    check(sizeof(FlacDecoder) < 48 * 1024, "係全体は48KB未満");
}

// ================================================================
// SoundFunctions
// ================================================================

static bool plugged = true;
static int ReadHook(int pin){
    if(pin != AUDIO_DETECT) return -1;
    return plugged ? LOW : HIGH;
}
static I2S& Out(){ return *I2S::last; }
static int16_t Left(uint32_t w){ return (int16_t)(w >> 16); }
static int16_t Right(uint32_t w){ return (int16_t)(w & 0xFFFF); }

static void Step(unsigned long now){
    SoundFunctions::UpdateAt(now);
    SoundFunctions::Core1StepAt(now);
}

// I2Sが全部送ったことにして進める。書かれた左右を返す
static void Run(unsigned long& now, int steps, std::vector<int16_t>* l = nullptr, std::vector<int16_t>* r = nullptr){
    for(int i = 0; i < steps; i++){
        Out().consume(Out().queued.size());
        const size_t before = Out().written.size();
        now += 1;
        Step(now);
        for(size_t k = before; k < Out().written.size(); k++){
            if(l) l->push_back(Left(Out().written[k]));
            if(r) r->push_back(Right(Out().written[k]));
        }
    }
}

static double Rms(const std::vector<int16_t>& v, size_t from, size_t to){
    if(to > v.size()) to = v.size();
    if(from >= to) return 0;
    double sum = 0;
    for(size_t i = from; i < to; i++) sum += (double)v[i] * v[i];
    return sqrt(sum / (double)(to - from));
}

static void TestSoundFunctions(){
    using namespace SoundFunctions;
    printf("\n===== SoundFunctions =====\n");
    OSData::SD_usable = true;
    HostGpio::read_hook = &ReadHook;
    static unsigned long now = 1000;
    SetupAt(now);
    check(SetSampleRate(kSampleRateHigh), "44100Hzで鳴らす");
    Step(now);
    for(int i = 0; i < 3; i++){ now += 1; Step(now); }
    check(Out().running, "I2Sが動く");
    Put("/music/st.flac", "kFlacStereo16");
    Put("/music/mono.FLAC", "kFlacMono1000");
    HostSd::files["/music/st.mp3"] = std::string((const char*)kMp3CbrStereo, sizeof(kMp3CbrStereo));
    HostSd::files["/music/bad.flac"] = "not a flac";

    printf("--- 鳴らす ---\n");
    {
        const char* err = nullptr;
        WavInfo info;
        check(WavPlay("/music/st.flac", false, 100, &err, &info), "FLACを鳴らせる");
        check(info.channels == 2 && info.sample_rate == 44100 && info.bits == 16 && info.duration_ms == 300, "情報を返す");
        check(WavDurationMs() == 300 && strcmp(WavTitle(), "st.flac") == 0, "長さと名前");
        std::vector<int16_t> l, r;
        Run(now, 200, &l, &r);
        printf("       %zuフレーム 左RMS %.0f 右RMS %.0f\n", l.size(), Rms(l, 1000, 12000), Rms(r, 1000, 12000));
        check(!WavPlaying(), "鳴り終えたら止まる");
        check(Rms(l, 1000, 12000) > 300 && Rms(r, 1000, 12000) > 300, "左右とも鳴る");
        size_t last = 0;
        for(size_t i = 0; i < l.size(); i++) if(abs(l[i]) > 100 || abs(r[i]) > 100) last = i;
        printf("       音のある最後のサンプル %zu\n", last);
        check(last >= 13000 && last <= 13230 + 2048, "長さどおり(I2Sのバッファのずれまで)");
        //ステレオの左右が元のまま出ている(左=440Hzと3000Hz、右=440Hzと660Hz。RMSの比で見る)
        check(Rms(l, 1000, 12000) > Rms(r, 1000, 12000) * 0.5 && Rms(l, 1000, 12000) < Rms(r, 1000, 12000) * 2.5, "左右の大きさが元の音に近い");
    }
    {
        //22.05kHzの1kHzが、44.1kHzの出力でも1kHzのまま鳴る
        check(WavPlay("/music/mono.FLAC", false, 100), "拡張子の大小は区別しない・22.05kHzのFLAC");
        std::vector<int16_t> l;
        Run(now, 200, &l);
        int ups = 0;
        for(size_t i = 1001; i < 8000 && i < l.size(); i++) if(l[i - 1] < 0 && l[i] >= 0) ups++;
        const double f = ups * 44100.0 / 6999.0;
        printf("       周波数 %.1fHz\n", f);
        check(fabs(f - 1000) < 20, "周波数の変換を通して1kHz");
    }

    printf("--- シーク・一時停止 ---\n");
    {
        check(WavPlay("/music/st.flac", false, 100), "鳴らす");
        Run(now, 3);
        check(WavSeekMs(150), "飛べる");
        const uint32_t pos = WavPositionMs();
        printf("       位置 %ums\n", (unsigned)pos);
        check(pos >= 100 && pos <= 250, "位置は飛んだ先");
        check(WavPause(true) && WavPaused(), "止められる");
        WavStop();
        check(!WavPlaying() && !WavSeekMs(100), "止めたら飛べない");
    }

    printf("--- MP3・WAVとの行き来 ---\n");
    {
        const char* err = nullptr;
        check(!WavPlay("/music/bad.flac", false, 100, &err) && err && strstr(err, "FLAC") != nullptr, "読めなければ理由を返す");
        check(WavPlay("/music/st.mp3", false, 100), "MP3を鳴らす");
        Run(now, 20);
        check(WavPlay("/music/st.flac", false, 100), "鳴らしている途中でFLACへ切り替えられる(同じ領域を使う)");
        std::vector<int16_t> l;
        Run(now, 200, &l);
        check(!WavPlaying() && l.size() > 10000, "FLACを鳴らし終える");
        check(WavPlay("/music/st.mp3", false, 100), "FLACの後にMP3を鳴らせる");
        Run(now, 400);
        check(!WavPlaying(), "MP3を鳴らし終える");
        std::string wav = "RIFF____WAVEfmt ";
        auto put16 = [&](uint32_t v){ wav += (char)(v & 0xFF); wav += (char)(v >> 8); };
        auto put32 = [&](uint32_t v){ put16(v & 0xFFFF); put16(v >> 16); };
        put32(16); put16(1); put16(1); put32(44100); put32(88200); put16(2); put16(16);
        wav += "data"; put32(2000);
        for(int i = 0; i < 1000; i++) put16(6000);
        HostSd::files["/music/c.wav"] = wav;
        check(WavPlay("/music/c.wav", false, 100), "WAVを鳴らせる(領域は返す)");
        std::vector<int16_t> w;
        Run(now, 50, &w);
        size_t n = 0;
        for(int16_t s : w) if(s == 3000) n++;
        check(n == 1000, "WAVはそのまま鳴る");
        check(WavPlay("/music/st.flac", false, 100), "WAVの後にFLACを鳴らせる");
        Run(now, 200);
        check(!WavPlaying(), "鳴り終える");
    }

    printf("--- 曲と曲の間を空けない ---\n");
    {
        check(WavPlay("/music/st.flac", false, 100), "1曲目(FLAC)を鳴らす");
        const uint32_t serial = PlaybackSerial();
        const uint32_t adv = WavAdvanceCount();
        check(WavQueueNext("/music/st.mp3"), "次の曲(MP3)を予約する");
        std::vector<int16_t> l;
        for(int i = 0; i < 600 && WavAdvanceCount() == adv; i++) Run(now, 1, &l);
        check(WavAdvanceCount() == adv + 1 && strcmp(WavTitle(), "st.mp3") == 0, "FLACからMP3へ形式をまたいで続けて移る");
        check(l.size() >= 13230 - 2048 && l.size() <= 13230 + 2048, "1曲目を鳴らし終えたところで移る");
        check(PlaybackSerial() == serial, "他のアプリに取られた扱いにしない");
        Run(now, 600, &l);
        check(!WavPlaying(), "2曲目の終わりで止まる");
        check(WavPlay("/music/st.mp3", false, 100) && WavQueueNext("/music/st.flac"), "MP3の次にFLACを予約");
        const uint32_t adv2 = WavAdvanceCount();
        for(int i = 0; i < 800 && WavAdvanceCount() == adv2; i++) Run(now, 1);
        check(WavAdvanceCount() == adv2 + 1 && strcmp(WavTitle(), "st.flac") == 0, "MP3からFLACへも続けて移る");
        Run(now, 600);
        check(!WavPlaying(), "止まる");
        check(WavPlay("/music/st.flac", false, 100) && WavQueueNext("/music/mono.FLAC"), "チャンネル数が違う曲を予約して");
        for(int i = 0; i < 600 && WavPlaying(); i++) Run(now, 1);
        check(!WavPlaying() && strcmp(WavTitle(), "st.flac") == 0, "続けず1曲で終わる(ミュージックが改めて鳴らす)");
    }

    printf("--- 96kHzの曲と出力の周波数の系統(44.1kHz系/48kHz系) ---\n");
    {
        Put("/music/hi96.flac", "kFlacStereo24At96k");
        //44100Hzで44.1kHzの曲を鳴らしている(変換なし)ときは、48kHz系の曲を続けて積まない(間を空けて切り替える)
        check(SampleRate() == kSampleRateHigh && WavPlay("/music/st.flac", false, 100) && WavQueueNext("/music/hi96.flac"),
              "44.1kHzの曲の次に96kHzの曲を予約");
        for(int i = 0; i < 600 && WavPlaying(); i++) Run(now, 1);
        check(!WavPlaying() && strcmp(WavTitle(), "st.flac") == 0, "系統が違えば続けず1曲で終わる");
        WavInfo info;
        check(WavPlay("/music/hi96.flac", false, 100, nullptr, &info) && info.sample_rate == 96000 && info.bits == 24,
              "96kHz/24bitの曲を(44100Hzへ下げて)鳴らせる");
        Run(now, 600);
        //48000Hzなら48kHz系の曲同士は続けて積む。1曲目は先読み(kWavPrefillSamples)より長い48kHzステレオ0.5秒のWAV
        //(先読みの間に読み終える短い曲は、予約より先に読み取り係を閉じるので続けて積まない。もとからの決まり)
        {
            std::string wav = "RIFF____WAVEfmt ";
            auto put16 = [&](uint32_t v){ wav += (char)(v & 0xFF); wav += (char)(v >> 8); };
            auto put32 = [&](uint32_t v){ put16(v & 0xFFFF); put16(v >> 16); };
            put32(16); put16(1); put16(2); put32(48000); put32(192000); put16(4); put16(16);
            wav += "data"; put32(24000 * 4);
            for(int i = 0; i < 24000; i++){ put16(5000); put16(5000); }
            HostSd::files["/music/k48.wav"] = wav;
        }
        check(RequestSampleRate(kSampleRate48k), "48000Hzを頼む");
        Run(now, 3);
        check(Out().sample_rate == (long)kSampleRate48k, "48000Hzで開き直す");
        const uint32_t adv = WavAdvanceCount();
        check(WavPlay("/music/k48.wav", false, 100) && WavQueueNext("/music/hi96.flac"), "48kHzの曲の次に96kHzの曲を予約");
        std::vector<int16_t> l;
        for(int i = 0; i < 800 && WavAdvanceCount() == adv; i++) Run(now, 1, &l);
        check(WavAdvanceCount() == adv + 1 && strcmp(WavTitle(), "hi96.flac") == 0, "同じ系統なら続けて移る");
        size_t same = 0;
        for(int16_t v : l) if(v == 2500) same++;
        printf("       1曲目 %zu/%zuサンプルがそのまま(48kHzで24000)\n", same, l.size());
        check(same >= 24000 - 2048 && l.size() <= 24000 + 2048, "1曲目は変換せずに鳴り終えたところで移る");
        Run(now, 600);
        check(!WavPlaying(), "2曲目の終わりで止まる");
        //22050Hz(どちらの系統も変換する)なら、系統をまたいでも続けて積む
        check(RequestSampleRate(kSampleRateLow), "22050Hzを頼む");
        Run(now, 3);
        const uint32_t adv2 = WavAdvanceCount();
        check(WavPlay("/music/k48.wav", false, 100) && WavQueueNext("/music/st.flac"), "22050Hzで48kHzの次に44.1kHzを予約");
        for(int i = 0; i < 800 && WavAdvanceCount() == adv2; i++) Run(now, 1);
        check(WavAdvanceCount() == adv2 + 1 && strcmp(WavTitle(), "st.flac") == 0, "どちらも変換するなら続けて移る");
        Run(now, 600);
        RequestSampleRate(0);
        Run(now, 3);
    }

    printf("--- ミュージックは曲に合わせて44100/48000Hzを選ぶ ---\n");
    {
        //画面(MusicScene)と同じく、毎フレーム MusicService の望みを要求として渡す
        auto frame = [&](int n){
            for(int i = 0; i < n; i++){
                RequestSampleRate(MusicService::PreferredSampleRate());
                MusicService::Update();
                Run(now, 1);
            }
        };
        MusicService::Test::Reset();
        check(MusicService::PreferredSampleRate() == kSampleRateHigh, "鳴らす前は44100Hzを望む");
        MusicService::Track t[3];
        t[0].name.assign("st.flac"); t[1].name.assign("hi96.flac"); t[2].name.assign("st.mp3");
        RequestSampleRate(MusicService::PreferredSampleRate());
        check(MusicService::StartPlaylist(t, 3, nullptr, 0, -1, "すべての曲", 0), "44.1kHz→96kHz→44.1kHzの並びを鳴らす");
        frame(2);
        check(MusicService::Current() == 0 && SampleRate() == kSampleRateHigh, "1曲目(44.1kHz)は44100Hz");
        bool ok = false;
        for(int i = 0; i < 1500 && !ok; i++){ frame(1); ok = MusicService::Current() == 1; }
        frame(3);
        check(ok && MusicService::Active() && !MusicService::Interrupted(), "2曲目へ進み、止まった扱いにならない");
        check(SampleRate() == kSampleRate48k && MusicService::PreferredSampleRate() == kSampleRate48k
              && strcmp(WavTitle(), "hi96.flac") == 0 && WavPlaying(), "2曲目(96kHz)は48000Hzへ切り替えて鳴らす");
        ok = false;
        for(int i = 0; i < 1500 && !ok; i++){ frame(1); ok = MusicService::Current() == 2; }
        frame(3);
        check(ok && MusicService::Active() && SampleRate() == kSampleRateHigh && strcmp(WavTitle(), "st.mp3") == 0,
              "3曲目(44.1kHzのMP3)は44100Hzへ戻す");
        //他の画面が周波数を決めていれば(22050Hz)、それに従って変換して鳴らす
        MusicService::Stop();
        frame(2);
        RequestSampleRate(kSampleRateLow);
        MusicService::PlayTrack(1);
        for(int i = 0; i < 3; i++){ MusicService::Update(); Run(now, 1); }
        check(MusicService::Active() && SampleRate() == kSampleRateLow && MusicService::PreferredSampleRate() == kSampleRateHigh,
              "他の画面が22050Hzを望んでいれば切り替えない");
        MusicService::Test::Reset();
        RequestSampleRate(0);
        Run(now, 3);
    }
}

int main(){
    TestSoundFunctions();
    TestTags();
    TestBitExact();
    TestOutputs();
    TestHiRes();
    TestContainer();
    TestSeek();
    TestLoop();
    TestErrors();
    TestDamaged();
    TestMemory();
    printf("\n%s (%d 失敗)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
