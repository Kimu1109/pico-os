// ALAC(.m4a)の再生の検証。
//
// 前半は AlacDecoder(MP4の解析・ビット単位の一致・左右・周波数の変換・シーク・ループ・断り方・壊れたファイル)。
// .m4a は data/alac/*.m4a(gen_alac_test_data.py が ffmpeg で作ったもの)を stubs/SdFat.h の HostSd::files へ置く。
// 正解は ffmpeg の展開結果のハッシュ(alac_test_data.hpp)。**元の周波数のまま読めばビット単位で一致する**。
// moov が mdat の後ろ(ffmpegの既定)と前(faststart)、チャンクが複数・stsc が複数の組のファイルも通す。
//
// 後半は SoundFunctions の配線(WavPlay が .m4a を AlacDecoder で読む・MP3/FLACと行き来できる・曲と曲の間を空けない)と、
// ミュージックの索引が使うタグ(iTunes形式)。
#include "hal/Sound_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Alac_Decoder.hpp"
#include "sound/Audio_Tags.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"
#include "alac_test_data.hpp"
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

// data/alac/<名前>.m4a を読む(このファイルの隣)
static std::string Load(const char* name){
    std::string here = __FILE__;
    here = here.substr(0, here.find_last_of('/') + 1) + "data/alac/" + name + ".m4a";
    std::ifstream in(here, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void Put(const char* path, const char* name){ HostSd::files[path] = Load(name); }

static std::vector<int16_t> ReadAll(AlacDecoder& dec, size_t limit = 2000000){
    std::vector<int16_t> all;
    int16_t buf[512];
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

static double Freq(const std::vector<int16_t>& v, size_t from, size_t to, uint32_t rate){
    int ups = 0;
    for(size_t i = from + 1; i < to; i++) if(v[i - 1] < 0 && v[i] >= 0) ups++;
    return (double)ups * rate / (double)(to - from);
}

// 設定(cookie)の項目を書き換える。cookie は2つ目の 'alac'(標本記述の中)の版/フラグ4バイトの後ろの24バイト
static std::string PatchCookie(std::string f, int frame_length, int bps, int channels, int rate){
    const size_t a = f.find("alac");
    const size_t b = f.find("alac", a + 1);
    unsigned char* c = (unsigned char*)&f[b + 4 + 4];
    if(frame_length >= 0){ c[0] = (unsigned char)(frame_length >> 24); c[1] = (unsigned char)(frame_length >> 16); c[2] = (unsigned char)(frame_length >> 8); c[3] = (unsigned char)frame_length; }
    if(bps >= 0) c[5] = (unsigned char)bps;
    if(channels >= 0) c[9] = (unsigned char)channels;
    if(rate >= 0){ c[20] = (unsigned char)(rate >> 24); c[21] = (unsigned char)(rate >> 16); c[22] = (unsigned char)(rate >> 8); c[23] = (unsigned char)rate; }
    return f;
}

struct Case { const char* name; unsigned frames; unsigned long long hash; int channels; int bps; uint32_t rate; };

static void TestBitExact(){
    OSData::SD_usable = true;
    const Case cases[] = {
        {"kAlacStereo16",      kAlacStereo16Frames,      kAlacStereo16Hash,      2, 16, 44100},
        {"kAlacStereo16Fast",  kAlacStereo16FastFrames,  kAlacStereo16FastHash,  2, 16, 44100},
        {"kAlacStereo16Order", kAlacStereo16OrderFrames, kAlacStereo16OrderHash, 2, 16, 44100},
        {"kAlacMono",          kAlacMonoFrames,          kAlacMonoHash,          1, 16, 22050},
        {"kAlacStereo24",      kAlacStereo24Frames,      kAlacStereo24Hash,      2, 24, 48000},
        {"kAlacStereo24At96k", kAlacStereo24At96kFrames, kAlacStereo24At96kHash, 2, 24, 96000},
        {"kAlacSilence",       kAlacSilenceFrames,       kAlacSilenceHash,       2, 16, 44100},
        {"kAlacNoise",         kAlacNoiseFrames,         kAlacNoiseHash,         2, 16, 44100},
        {"kAlacChirp",         kAlacChirpFrames,         kAlacChirpHash,         1, 16, 22050},
        {"kAlacChunks",        kAlacChunksFrames,        kAlacChunksHash,        1, 16, 22050},
        {"kAlacTagged",        kAlacTaggedFrames,        kAlacTaggedHash,        1, 16, 22050},
    };
    printf("--- ffmpegの展開結果とビット単位で一致 ---\n");
    for(const Case& c : cases){
        std::string path = std::string("/") + c.name + ".m4a";
        Put(path.c_str(), c.name);
        AlacDecoder dec;
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
    Put("/st.m4a", "kAlacStereo16");
    std::vector<int16_t> stereo;
    {
        AlacDecoder dec;
        dec.setStereoOutput(true);
        dec.open("/st.m4a", 44100);
        stereo = ReadAll(dec);
    }
    {
        AlacDecoder dec;
        check(dec.open("/st.m4a", 44100) && dec.outChannels() == 1, "既定はモノラル出力");
        const auto mono = ReadAll(dec);
        bool same = mono.size() * 2 == stereo.size();
        for(size_t i = 0; same && i < mono.size(); i++){
            same = mono[i] == (int16_t)(((int32_t)stereo[2 * i] + stereo[2 * i + 1]) / 2);
        }
        check(same, "モノラルは左右の平均");
    }
    {
        AlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/st.m4a", 22050) && dec.outChannels() == 2, "22050Hzへ下げて開ける");
        const size_t frames = ReadAll(dec).size() / 2;
        printf("       %zuフレーム\n", frames);
        check(frames >= 6600 && frames <= 6630, "フレーム数は半分");
    }
    {
        Put("/m.m4a", "kAlacMono");     //22.05kHzの1kHz(kFlacMono1000 と違い 440Hz ではなく 1kHz の正弦波)
        AlacDecoder dec;
        check(dec.open("/m.m4a", 44100) && dec.outChannels() == 1, "モノラルを44.1kHzへ上げて開ける");
        const auto all = ReadAll(dec);
        printf("       %zuフレーム %.1fHz\n", all.size(), Freq(all, 200, all.size() - 200, 44100));
        check(all.size() >= 8815 && all.size() <= 8825 && fabs(Freq(all, 200, all.size() - 200, 44100) - 1000) < 15, "フレーム数は倍・音は1kHzのまま");
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
    Put("/hi96.m4a", "kAlacStereo24At96k");
    std::vector<int16_t> native;
    {
        AlacDecoder dec;
        dec.setStereoOutput(true);
        dec.open("/hi96.m4a", 96000);
        native = ReadAll(dec);
    }
    const size_t nf = native.size() / 2;
    const double rms_l = RmsLane(native, 2, 0, nf / 5, nf * 4 / 5), rms_r = RmsLane(native, 2, 1, nf / 5, nf * 4 / 5);
    const uint32_t outs[] = {44100, 48000, 22050};
    for(uint32_t out : outs){
        AlacDecoder dec;
        dec.setStereoOutput(true);
        char label[160];
        snprintf(label, sizeof(label), "96kHzを%uHzへ下げて開ける", (unsigned)out);
        check(dec.open("/hi96.m4a", out) && dec.outChannels() == 2 && dec.sampleRate() == 96000, label);
        const auto all = ReadAll(dec);
        const size_t frames = all.size() / 2;
        const size_t want = (size_t)((uint64_t)kAlacStereo24At96kFrames * out / 96000);
        snprintf(label, sizeof(label), "  %uHz: フレーム数 %zu / 期待 %zu", (unsigned)out, frames, want);
        check(dec.finished() && frames + 3 >= want && frames <= want + 3, label);
        const double l = RmsLane(all, 2, 0, frames / 5, frames * 4 / 5), r = RmsLane(all, 2, 1, frames / 5, frames * 4 / 5);
        snprintf(label, sizeof(label), "  %uHz: 大きさは元のまま(左 %.0f/%.0f 右 %.0f/%.0f)", (unsigned)out, l, rms_l, r, rms_r);
        check(fabs(l - rms_l) < rms_l * 0.03 && fabs(r - rms_r) < rms_r * 0.03, label);
    }
    {
        //シークも元の周波数で数える(50msの位置は元の4800サンプル目)
        AlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/hi96.m4a", 48000) && dec.durationMs() == 100, "96kHz: 長さ100ms");
        check(dec.seekMs(50), "96kHz: 50msへ飛べる");
        const auto rest = ReadAll(dec);
        const size_t frames = rest.size() / 2;
        char label[96];
        snprintf(label, sizeof(label), "96kHz: 飛んだ先から残り %zu フレーム(48kHzで約2400)", frames);
        check(frames + 40 >= 2400 && frames <= 2400 + 40, label);
    }
}

static void TestSeek(){
    printf("--- シーク(1チャンク / 複数のチャンク・stscが複数の組) ---\n");
    OSData::SD_usable = true;
    const char* names[2] = {"kAlacChirp", "kAlacChunks"};
    for(int k = 0; k < 2; k++){
        std::string path = std::string("/") + names[k] + ".m4a";
        Put(path.c_str(), names[k]);
        AlacDecoder full;
        full.open(path.c_str(), 22050);
        const auto ref = ReadAll(full);

        AlacDecoder dec;
        check(dec.open(path.c_str(), 22050), "開ける");
        printf("       フレーム数 %u・総サンプル %llu\n", (unsigned)dec.frameCount(), (unsigned long long)dec.totalSamples());
        const uint32_t points[] = {0, 1, 100, 185, 186, 333, 500, 777, 1000, 1199, 5000};
        for(uint32_t ms : points){
            check(dec.seekMs(ms), "飛べる");
            const auto got = ReadAll(dec);
            size_t at = (size_t)((uint64_t)ms * 22050 / 1000);
            if(at >= ref.size()) at = ref.size() - 1;
            const bool ok = got.size() == ref.size() - at && memcmp(got.data(), ref.data() + at, got.size() * sizeof(int16_t)) == 0;
            char label[96];
            snprintf(label, sizeof(label), "%s: %ums へ飛んだ後はサンプル %zu から同じ音(%zu個)", names[k], (unsigned)ms, at, got.size());
            check(ok, label);
        }
        dec.seekMs(800);
        int16_t tmp[300];
        dec.read(tmp, 300);
        dec.seekMs(200);
        const auto back = ReadAll(dec);
        check(back.size() == ref.size() - 4410 && memcmp(back.data(), ref.data() + 4410, back.size() * 2) == 0, "読みかけから前へ戻れる");
        dec.close();
        check(dec.seekMs(500) && dec.isOpen(), "閉じた後でも開き直して飛べる");
        const auto re = ReadAll(dec);
        check(re.size() == ref.size() - 11025 && memcmp(re.data(), ref.data() + 11025, re.size() * 2) == 0, "開き直した後も同じ音");
    }
}

static void TestLoop(){
    printf("--- ループ ---\n");
    OSData::SD_usable = true;
    Put("/m.m4a", "kAlacMono");
    AlacDecoder dec;
    dec.setLoop(true);
    check(dec.open("/m.m4a", 22050), "ループで開ける");
    const auto two = ReadAll(dec, kAlacMonoFrames * 2 + 100);
    check(!dec.finished() && two.size() == kAlacMonoFrames * 2 + 100, "終わりまで来ても終わらない");
    bool same = true;
    for(size_t i = 0; i < kAlacMonoFrames; i++) same = same && two[i] == two[kAlacMonoFrames + i];
    check(same, "2周目は頭から同じ音(継ぎ目に余分な無音が無い)");
}

static void TestErrors(){
    printf("--- 断り方 ---\n");
    OSData::SD_usable = true;
    const std::string base = Load("kAlacStereo16");
    AlacDecoder dec;
    HostSd::files["/bad.m4a"] = "hello, this is not an m4a file, but it is long enough to look at.";
    check(!dec.open("/bad.m4a", 22050) && dec.error() == AlacDecoder::Error::NotAlac, "M4Aでなければ NotAlac");
    HostSd::files["/empty.m4a"] = "";
    check(!dec.open("/empty.m4a", 22050) && dec.error() == AlacDecoder::Error::NotAlac, "空は NotAlac");
    check(!dec.open("/nothing.m4a", 22050) && dec.error() == AlacDecoder::Error::OpenFailed, "無いファイルは OpenFailed");
    HostSd::files["/a.m4a"] = Load("kAacOnly");
    check(!dec.open("/a.m4a", 22050) && dec.error() == AlacDecoder::Error::Aac, "AACのM4Aは Aac で断る");
    printf("       理由の文: %s\n", AlacDecoder::ErrorText(AlacDecoder::Error::Aac));
    HostSd::files["/flac.m4a"] = std::string("fLaC") + std::string(200, 'x');
    check(!dec.open("/flac.m4a", 22050) && dec.error() == AlacDecoder::Error::NotAlac, "中身がFLACなら NotAlac");
    //moov が mdat の後ろにあるファイルが途中で切れて moov が無い
    HostSd::files["/nomoov.m4a"] = base.substr(0, base.size() - 400);
    check(!dec.open("/nomoov.m4a", 22050), "moov が無ければ開けない");
    HostSd::files["/hi.m4a"] = PatchCookie(base, -1, -1, -1, 192000);
    check(!dec.open("/hi.m4a", 22050) && dec.error() == AlacDecoder::Error::Unsupported, "192kHz は Unsupported");
    HostSd::files["/6ch.m4a"] = PatchCookie(base, -1, -1, 6, -1);
    check(!dec.open("/6ch.m4a", 22050) && dec.error() == AlacDecoder::Error::Unsupported, "6ch は Unsupported");
    HostSd::files["/32b.m4a"] = PatchCookie(base, -1, 32, -1, -1);
    check(!dec.open("/32b.m4a", 22050) && dec.error() == AlacDecoder::Error::Unsupported, "32bit は Unsupported");
    HostSd::files["/fl.m4a"] = PatchCookie(base, 8192, -1, -1, -1);
    check(!dec.open("/fl.m4a", 22050) && dec.error() == AlacDecoder::Error::Unsupported, "フレーム長8192 は Unsupported");
    OSData::SD_usable = false;
    check(!dec.open("/st.m4a", 22050) && dec.error() == AlacDecoder::Error::NoSd, "SDが無ければ NoSd");
    OSData::SD_usable = true;
}

static void TestDamaged(){
    printf("--- 壊れたファイル ---\n");
    OSData::SD_usable = true;
    const std::string base = Load("kAlacStereo16");
    AlacDecoder ref_dec;
    ref_dec.setStereoOutput(true);
    HostSd::files["/ok.m4a"] = base;
    ref_dec.open("/ok.m4a", 44100);
    const auto ref = ReadAll(ref_dec);
    {
        //音のデータ(mdat。ffmpegは頭の36バイトの後ろ)の中を壊す: そのフレームは無音になり、長さは変わらない・後ろのフレームは元の音のまま
        std::string f = base;
        const size_t mid = 36 + 4096;
        for(size_t i = 0; i < 60; i++) f[mid + i] = (char)(f[mid + i] ^ 0x5A);
        HostSd::files["/dmg.m4a"] = f;
        AlacDecoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/dmg.m4a", 44100), "音の中が壊れていても開ける");
        const auto all = ReadAll(dec);
        check(dec.finished() && all.size() == ref.size(), "壊れたフレームは同じ長さの無音で置き換わる(長さがずれない)");
        const size_t tail = 4096 * 2;
        check(memcmp(all.data() + all.size() - tail, ref.data() + ref.size() - tail, tail * 2) == 0, "壊れていない後ろのフレームは元の音のまま");
    }
    {
        //ランダムに壊した60本(音のデータも表も): クラッシュ・無限ループ・メモリ違反(ASan/UBSan)が無いこと
        uint32_t seed = 4321;
        auto rnd = [&]{ seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const char* names[] = {"kAlacStereo16", "kAlacStereo24", "kAlacNoise", "kAlacChunks"};
        int opened = 0;
        for(int t = 0; t < 60; t++){
            std::string f = Load(names[t % 4]);
            const int hits = 1 + (int)(rnd() % 8);
            for(int h = 0; h < hits; h++) f[(rnd() % f.size())] = (char)rnd();
            if(t % 5 == 0) f.resize(f.size() - rnd() % (f.size() / 2));
            HostSd::files["/fuzz.m4a"] = f;
            AlacDecoder d;
            d.setStereoOutput(true);
            if(!d.open("/fuzz.m4a", (t & 1) ? 44100 : 22050)) continue;
            opened++;
            ReadAll(d, 200000);
            d.seekMs(rnd() % 500);
            ReadAll(d, 200000);
        }
        printf("       壊した60本のうち %d 本は開けて、最後まで読めた\n", opened);
        check(opened > 0, "壊したファイル60本を読み切れる(落ちない・止まらない)");
    }
    {
        //表(moov)の中だけを重点的に壊す
        uint32_t seed = 777;
        auto rnd = [&]{ seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        const size_t moov = base.rfind("moov") - 4;
        int n = 0;
        for(int t = 0; t < 60; t++){
            std::string f = Load(t % 2 ? "kAlacChunks" : "kAlacStereo16");
            const size_t m = f.rfind("moov") - 4;
            for(int h = 0; h < 3; h++) f[m + rnd() % (f.size() - m)] = (char)rnd();
            HostSd::files["/fuzz2.m4a"] = f;
            AlacDecoder d;
            d.setStereoOutput(true);
            if(!d.open("/fuzz2.m4a", 44100)) continue;
            n++;
            ReadAll(d, 200000);
            d.seekMs(rnd() % 300);
            ReadAll(d, 200000);
        }
        (void)moov;
        printf("       表を壊した60本のうち %d 本は開けた\n", n);
        check(true, "表を壊したファイルでも落ちない・止まらない");
    }
}

// ================================================================
// タグ(iTunes形式)
// ================================================================

static void TestTags(){
    printf("--- タグ(iTunes形式) ---\n");
    OSData::SD_usable = true;
    AudioTags t;
    HostSd::files["/music/tag.m4a"] = Load("kAlacTagged");
    check(AudioTagReader::Read("/music/tag.m4a", t), "M4Aのタグを読める(moov が mdat の後ろでも)");
    check(strcmp(t.title.c_str(), "テスト曲") == 0, "曲名(UTF-8)");
    check(strcmp(t.artist.c_str(), "Some Band") == 0 && strcmp(t.album.c_str(), "First Album") == 0 && t.track == 3, "アーティスト・アルバム・曲番号");
    HostSd::files["/music/Up.M4A"] = Load("kAlacTagged");
    check(AudioTagReader::Read("/music/Up.M4A", t) && t.track == 3, "拡張子の大小は区別しない");
    HostSd::files["/music/notag.m4a"] = Load("kAlacMono");
    check(!AudioTagReader::Read("/music/notag.m4a", t), "タグが無ければ false");
    HostSd::files["/music/cut.m4a"] = Load("kAlacTagged").substr(0, 100);
    AudioTagReader::Read("/music/cut.m4a", t);
    check(true, "途中で切れていても落ちない");
    check(MusicService::IsPcmFile("a.m4a") && MusicService::IsPcmFile("A.M4A") && !MusicService::IsPcmFile("m4a"), "ミュージックは .m4a を鳴らすファイルとして扱う");
}

static void TestMemory(){
    printf("--- 大きさ ---\n");
    printf("       AlacDecoder %zuB\n", sizeof(AlacDecoder));
    check(sizeof(AlacDecoder) < 52 * 1024, "係全体は52KB未満");
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
    Put("/music/st.m4a", "kAlacStereo16");
    Put("/music/mono.M4A", "kAlacMono");
    HostSd::files["/music/st.mp3"] = std::string((const char*)kMp3CbrStereo, sizeof(kMp3CbrStereo));
    HostSd::files["/music/st.flac"] = [] {
        std::string here = __FILE__;
        here = here.substr(0, here.find_last_of('/') + 1) + "data/flac/kFlacStereo16.flac";
        std::ifstream in(here, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    HostSd::files["/music/bad.m4a"] = "not an m4a at all, but long enough to be looked at by the parser";
    Put("/music/aac.m4a", "kAacOnly");

    printf("--- 鳴らす ---\n");
    {
        const char* err = nullptr;
        WavInfo info;
        check(WavPlay("/music/st.m4a", false, 100, &err, &info), "ALACを鳴らせる");
        check(info.channels == 2 && info.sample_rate == 44100 && info.bits == 16 && info.duration_ms == 300, "情報を返す");
        check(WavDurationMs() == 300 && strcmp(WavTitle(), "st.m4a") == 0, "長さと名前");
        std::vector<int16_t> l, r;
        Run(now, 200, &l, &r);
        printf("       %zuフレーム 左RMS %.0f 右RMS %.0f\n", l.size(), Rms(l, 1000, 12000), Rms(r, 1000, 12000));
        check(!WavPlaying(), "鳴り終えたら止まる");
        check(Rms(l, 1000, 12000) > 300 && Rms(r, 1000, 12000) > 300, "左右とも鳴る");
        size_t last = 0;
        for(size_t i = 0; i < l.size(); i++) if(abs(l[i]) > 100 || abs(r[i]) > 100) last = i;
        check(last >= 13000 && last <= 13230 + 2048, "長さどおり(I2Sのバッファのずれまで)");
    }
    {
        check(WavPlay("/music/mono.M4A", false, 100), "拡張子の大小は区別しない・22.05kHzのALAC");
        std::vector<int16_t> l;
        Run(now, 200, &l);
        int ups = 0;
        for(size_t i = 1001; i < 8000 && i < l.size(); i++) if(l[i - 1] < 0 && l[i] >= 0) ups++;
        const double f = ups * 44100.0 / 6999.0;
        printf("       周波数 %.1fHz\n", f);
        check(fabs(f - 1000) < 20, "周波数の変換を通して1kHz");
    }
    {
        const char* err = nullptr;
        check(!WavPlay("/music/aac.m4a", false, 100, &err) && err && strstr(err, "AAC") != nullptr, "AACのM4Aは理由を返して断る");
        check(!WavPlay("/music/bad.m4a", false, 100, &err) && err && strstr(err, "ALAC") != nullptr, "読めなければ理由を返す");
    }

    printf("--- シーク・一時停止 ---\n");
    {
        check(WavPlay("/music/st.m4a", false, 100), "鳴らす");
        Run(now, 3);
        check(WavSeekMs(150), "飛べる");
        const uint32_t pos = WavPositionMs();
        printf("       位置 %ums\n", (unsigned)pos);
        check(pos >= 100 && pos <= 250, "位置は飛んだ先");
        check(WavPause(true) && WavPaused(), "止められる");
        WavStop();
        check(!WavPlaying() && !WavSeekMs(100), "止めたら飛べない");
    }

    printf("--- MP3・FLAC・WAVとの行き来 ---\n");
    {
        check(WavPlay("/music/st.mp3", false, 100), "MP3を鳴らす");
        Run(now, 20);
        check(WavPlay("/music/st.m4a", false, 100), "鳴らしている途中でALACへ切り替えられる(同じ領域を使う)");
        std::vector<int16_t> l;
        Run(now, 200, &l);
        check(!WavPlaying() && l.size() > 10000, "ALACを鳴らし終える");
        check(WavPlay("/music/st.flac", false, 100), "ALACの後にFLACを鳴らせる");
        Run(now, 200);
        check(!WavPlaying(), "FLACを鳴らし終える");
        check(WavPlay("/music/st.m4a", false, 100), "FLACの後にALACを鳴らせる");
        Run(now, 200);
        check(!WavPlaying(), "ALACを鳴らし終える");
        check(WavPlay("/music/st.mp3", false, 100), "ALACの後にMP3を鳴らせる");
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
        check(WavPlay("/music/st.m4a", false, 100), "WAVの後にALACを鳴らせる");
        Run(now, 200);
        check(!WavPlaying(), "鳴り終える");
    }

    printf("--- 曲と曲の間を空けない ---\n");
    {
        check(WavPlay("/music/st.m4a", false, 100), "1曲目(ALAC)を鳴らす");
        const uint32_t serial = PlaybackSerial();
        const uint32_t adv = WavAdvanceCount();
        check(WavQueueNext("/music/st.flac"), "次の曲(FLAC)を予約する");
        std::vector<int16_t> l;
        for(int i = 0; i < 600 && WavAdvanceCount() == adv; i++) Run(now, 1, &l);
        check(WavAdvanceCount() == adv + 1 && strcmp(WavTitle(), "st.flac") == 0, "ALACからFLACへ形式をまたいで続けて移る");
        check(l.size() >= 13230 - 2048 && l.size() <= 13230 + 2048, "1曲目を鳴らし終えたところで移る");
        check(PlaybackSerial() == serial, "他のアプリに取られた扱いにしない");
        Run(now, 600, &l);
        check(!WavPlaying(), "2曲目の終わりで止まる");
        check(WavPlay("/music/st.mp3", false, 100) && WavQueueNext("/music/st.m4a"), "MP3の次にALACを予約");
        const uint32_t adv2 = WavAdvanceCount();
        for(int i = 0; i < 800 && WavAdvanceCount() == adv2; i++) Run(now, 1);
        check(WavAdvanceCount() == adv2 + 1 && strcmp(WavTitle(), "st.m4a") == 0, "MP3からALACへも続けて移る");
        Run(now, 600);
        check(!WavPlaying(), "止まる");
        check(WavPlay("/music/st.m4a", false, 100) && WavQueueNext("/music/mono.M4A"), "チャンネル数が違う曲を予約して");
        for(int i = 0; i < 600 && WavPlaying(); i++) Run(now, 1);
        check(!WavPlaying() && strcmp(WavTitle(), "st.m4a") == 0, "続けず1曲で終わる(ミュージックが改めて鳴らす)");
    }
}

int main(){
    TestSoundFunctions();
    TestTags();
    TestBitExact();
    TestOutputs();
    TestHiRes();
    TestSeek();
    TestLoop();
    TestErrors();
    TestDamaged();
    TestMemory();
    printf("\n%s (%d 失敗)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
