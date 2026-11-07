// 24bitの音声出力(PICO_HW_SOUND_24BIT=1。音楽再生に特化した版)の検証。
//
// このテストだけ 24bitの版の定義で作り、音の経路のファイル(読み取り係・周波数の変換・先読みの列・SoundFunctions)を
// その定義で自分に入れる(CMakeLists.txt。ライブラリの同じファイルは16bitの版)。
//
// - 読み取り係: 24bitのFLAC/ALACを24bitの桁で読むと ffmpeg の展開結果(24bit)とビット単位で一致・16bitの桁なら今までどおり
//   16bitの展開結果と一致・16bitの曲は8bit上へずらしただけ・24bitのWAV・MP3・96kHzを48kHzへ下げても大きさが合う
// - SoundFunctions: ビット深度の切り替え(I2Sを32bitスロットで開き直す・バッファの大きさ・鳴っていた音を止める)、
//   sound.cfg の bit-depth、24bitのWAVの下位ビットが出力まで残る・効果音も鳴る・16bitへ戻す
#include "hal/Sound_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Flac_Decoder.hpp"
#include "sound/Alac_Decoder.hpp"
#include "sound/Wav_Decoder.hpp"
#include "sound/Mp3_Decoder.hpp"
#include "sound/Wav_Stream.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"
#include "flac_test_data.hpp"
#include "alac_test_data.hpp"
#include "mp3_test_data.hpp"

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

static_assert(PICO_HW_SOUND_24BIT == 1, "このテストは24bitの版の定義で作る");
static_assert(sizeof(PcmSample) == 4, "24bitの版の標本は32bit");
static_assert(WavStream::kRingSamples * sizeof(PcmSample) == 48 * 1024, "先読みの列は48KB");

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static std::string Load(const char* dir, const char* name, const char* ext){
    std::string here = __FILE__;
    here = here.substr(0, here.find_last_of('/') + 1) + "data/" + dir + "/" + name + ext;
    std::ifstream in(here, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::vector<int32_t> ReadAll(PcmFileDecoder& dec, size_t limit = 2000000){
    std::vector<int32_t> all;
    PcmSample buf[512];
    const size_t lanes = dec.outChannels();
    size_t frames = 0;
    while(!dec.finished() && frames < limit){
        const size_t n = dec.read(buf, 256);
        all.insert(all.end(), buf, buf + n * lanes);
        frames += n;
        if(n == 0) break;
    }
    return all;
}

// 16bitの値(16bitに切った値)のFNV-1a 64(flac_test と同じ)
static unsigned long long Fnv16(const std::vector<int32_t>& v){
    unsigned long long h = 0xCBF29CE484222325ULL;
    for(int32_t s : v) h = (h ^ (unsigned long long)(uint16_t)s) * 0x100000001B3ULL;
    return h;
}
// 24bitの値(32bitの符号付きとして)のFNV-1a 64(gen_*_test_data.py の fnv64_wide と同じ)
static unsigned long long Fnv32(const std::vector<int32_t>& v){
    unsigned long long h = 0xCBF29CE484222325ULL;
    for(int32_t s : v) h = (h ^ (unsigned long long)(uint32_t)s) * 0x100000001B3ULL;
    return h;
}

static double Rms(const std::vector<int32_t>& v, size_t lanes, size_t lane, size_t from, size_t to){
    double sum = 0;
    for(size_t i = from; i < to; i++){ const double x = v[i * lanes + lane]; sum += x * x; }
    return to > from ? sqrt(sum / (double)(to - from)) : 0;
}

template<class Dec>
static void TestBitExact(const char* label, const char* dir, const char* name, const char* ext, uint32_t rate,
                         unsigned frames, unsigned long long hash16, unsigned long long hash24){
    OSData::SD_usable = true;
    const std::string path = std::string("/") + name + ext;
    HostSd::files[path] = Load(dir, name, ext);
    char buf[200];
    {
        Dec dec;
        dec.setStereoOutput(true);
        dec.setWideOutput(true);
        check(dec.open(path.c_str(), rate) && dec.wideOutput(), (snprintf(buf, sizeof(buf), "%s: 24bitの桁で開ける", label), buf));
        const auto all = ReadAll(dec);
        snprintf(buf, sizeof(buf), "%s: 24bitのまま全サンプルが一致(%zu サンプル・ハッシュ %016llX)", label, all.size(), Fnv32(all));
        check(all.size() == (size_t)frames * 2 && Fnv32(all) == hash24, buf);
    }
    {
        Dec dec;
        dec.setStereoOutput(true);
        check(dec.open(path.c_str(), rate) && !dec.wideOutput(), (snprintf(buf, sizeof(buf), "%s: 既定は16bitの桁", label), buf));
        const auto all = ReadAll(dec);
        bool in16 = true;
        for(int32_t v : all) in16 = in16 && v >= -32768 && v <= 32767;
        snprintf(buf, sizeof(buf), "%s: 16bitの桁は今までどおり四捨五入した値(ハッシュ %016llX)", label, Fnv16(all));
        check(in16 && Fnv16(all) == hash16, buf);
    }
}

static void TestDecoders(){
    printf("--- 読み取り係の24bitの桁 ---\n");
    TestBitExact<FlacDecoder>("FLAC 48kHz/24bit", "flac", "kFlacStereo24", ".flac", 48000,
                              kFlacStereo24Frames, kFlacStereo24Hash, kFlacStereo24Hash24);
    TestBitExact<FlacDecoder>("FLAC 96kHz/24bit", "flac", "kFlacStereo24At96k", ".flac", 96000,
                              kFlacStereo24At96kFrames, kFlacStereo24At96kHash, kFlacStereo24At96kHash24);
    TestBitExact<AlacDecoder>("ALAC 48kHz/24bit", "alac", "kAlacStereo24", ".m4a", 48000,
                              kAlacStereo24Frames, kAlacStereo24Hash, kAlacStereo24Hash24);
    TestBitExact<AlacDecoder>("ALAC 96kHz/24bit", "alac", "kAlacStereo24At96k", ".m4a", 96000,
                              kAlacStereo24At96kFrames, kAlacStereo24At96kHash, kAlacStereo24At96kHash24);

    //16bitの曲を24bitの桁で読むと、16bitの値を8bit上へずらしただけ
    {
        HostSd::files["/s16.flac"] = Load("flac", "kFlacStereo16", ".flac");
        FlacDecoder n, w;
        n.setStereoOutput(true);
        w.setStereoOutput(true);
        w.setWideOutput(true);
        n.open("/s16.flac", 44100);
        w.open("/s16.flac", 44100);
        const auto a = ReadAll(n), b = ReadAll(w);
        bool same = a.size() == b.size() && !a.empty();
        for(size_t i = 0; same && i < a.size(); i++) same = b[i] == a[i] * 256;
        check(same, "16bitのFLACを24bitの桁で読むと8bit上へずらしただけ");
    }
    {
        HostSd::files["/st.mp3"] = std::string((const char*)kMp3CbrStereo, sizeof(kMp3CbrStereo));
        Mp3Decoder n, w;
        n.setStereoOutput(true);
        w.setStereoOutput(true);
        w.setWideOutput(true);
        n.open("/st.mp3", 44100);
        w.open("/st.mp3", 44100);
        const auto a = ReadAll(n), b = ReadAll(w);
        bool same = a.size() == b.size() && !a.empty();
        for(size_t i = 0; same && i < a.size(); i++) same = b[i] == a[i] * 256;
        check(same, "MP3を24bitの桁で読むと8bit上へずらしただけ");
    }
    //24bitのWAV(下位8bitまで読む)
    {
        std::string wav = "RIFF____WAVEfmt ";
        auto put16 = [&](uint32_t v){ wav += (char)(v & 0xFF); wav += (char)(v >> 8); };
        auto put32 = [&](uint32_t v){ put16(v & 0xFFFF); put16(v >> 16); };
        auto put24 = [&](int32_t v){ wav += (char)(v & 0xFF); wav += (char)((v >> 8) & 0xFF); wav += (char)((v >> 16) & 0xFF); };
        put32(16); put16(1); put16(2); put32(48000); put32(48000 * 6); put16(6); put16(24);
        wav += "data"; put32(1000 * 6);
        for(int i = 0; i < 1000; i++){ put24(0x123456 - i); put24(-0x234567 + i); }
        HostSd::files["/w24.wav"] = wav;
        WavDecoder w;
        w.setStereoOutput(true);
        w.setWideOutput(true);
        check(w.open("/w24.wav", 48000) && w.bitsPerSample() == 24, "24bitのWAVを開ける");
        const auto b = ReadAll(w);
        bool same = b.size() == 2000;
        for(int i = 0; same && i < 1000; i++) same = b[i * 2] == 0x123456 - i && b[i * 2 + 1] == -0x234567 + i;
        check(same, "24bitのWAVは下位8bitまでそのまま");
        WavDecoder n;
        n.setStereoOutput(true);
        n.open("/w24.wav", 48000);
        const auto a = ReadAll(n);
        check(a.size() == 2000 && a[0] == (0x123456 >> 8) && a[1] == (-0x234567 >> 8), "16bitの桁なら今までどおり上位16bit");
    }
    //96kHz/24bitを48kHzへ下げても、24bitの桁のまま大きさが合う(周波数の変換が64bitで足す)
    {
        HostSd::files["/hi.flac"] = Load("flac", "kFlacStereo24At96k", ".flac");
        FlacDecoder nat, down;
        nat.setStereoOutput(true); nat.setWideOutput(true);
        down.setStereoOutput(true); down.setWideOutput(true);
        nat.open("/hi.flac", 96000);
        down.open("/hi.flac", 48000);
        const auto a = ReadAll(nat), b = ReadAll(down);
        const size_t fa = a.size() / 2, fb = b.size() / 2;
        const double ra = Rms(a, 2, 0, fa / 5, fa * 4 / 5), rb = Rms(b, 2, 0, fb / 5, fb * 4 / 5);
        char buf[160];
        snprintf(buf, sizeof(buf), "96kHz→48kHz(24bitの桁): %zuフレーム・大きさ %.0f / %.0f", fb, rb, ra);
        check(fb + 3 >= fa / 2 && fb <= fa / 2 + 3 && ra > 1000000 && fabs(rb - ra) < ra * 0.03, buf);
        bool low_bits = false;
        for(int32_t v : b) low_bits = low_bits || (v & 0xFF) != 0;
        check(low_bits, "下げた後も下位8bitが残る");
    }
}

// ---------------------------------------------------------------- SoundFunctions

static bool plugged = true;
static int ReadHook(int pin){
    if(pin != AUDIO_DETECT) return -1;
    return plugged ? LOW : HIGH;
}
static I2S& Out(){ return *I2S::last; }
static void Step(unsigned long now){
    SoundFunctions::UpdateAt(now);
    SoundFunctions::Core1StepAt(now);
}
// I2Sが全部送ったことにして進め、書かれたワードを返す
static void Run(unsigned long& now, int steps, std::vector<uint32_t>* words = nullptr){
    for(int i = 0; i < steps; i++){
        Out().consume(Out().queued.size());
        const size_t before = Out().written.size();
        now += 1;
        Step(now);
        if(words) words->insert(words->end(), Out().written.begin() + (long)before, Out().written.end());
    }
}

static void TestSoundFunctions(){
    using namespace SoundFunctions;
    printf("--- SoundFunctions(24bitの出力) ---\n");
    OSData::SD_usable = true;
    HostGpio::read_hook = &ReadHook;
    static unsigned long now = 1000;
    HostSd::files["/sys/sound.cfg"] = "volume = 50\nsample-rate = 48000\n";
    SetupAt(now);
    Run(now, 3);
    check(BitDepth() == kBitDepth16 && Out().running && Out().bps == 16, "既定は16bit(16bitスロット)");
    check(IsBitDepthSupported(kBitDepth24) && !SetBitDepth(20) && !SetBitDepth(32), "この版は24bitを選べる・16/24以外は断る");
    const size_t cap16 = Out().capacity;

    //24bitのWAV(一定の値)を鳴らしてから切り替えると、止まる
    {
        std::string wav = "RIFF____WAVEfmt ";
        auto put16 = [&](uint32_t v){ wav += (char)(v & 0xFF); wav += (char)(v >> 8); };
        auto put32 = [&](uint32_t v){ put16(v & 0xFFFF); put16(v >> 16); };
        auto put24 = [&](int32_t v){ wav += (char)(v & 0xFF); wav += (char)((v >> 8) & 0xFF); wav += (char)((v >> 16) & 0xFF); };
        put32(16); put16(1); put16(2); put32(48000); put32(48000 * 6); put16(6); put16(24);
        wav += "data"; put32(24000 * 6);
        for(int i = 0; i < 24000; i++){ put24(0x123457); put24(-0x0ABCDF); }
        HostSd::files["/music/k24.wav"] = wav;
    }
    check(WavPlay("/music/k24.wav", false, 100), "16bitで24bitのWAVを鳴らす");
    Run(now, 5);
    const uint32_t serial = PlaybackSerial();
    check(SetBitDepth(kBitDepth24) && BitDepth() == kBitDepth24, "24bitへ切り替える");
    check(!WavPlaying() && PlaybackSerial() != serial, "鳴っていたWAVは止まる(他のアプリに取られた扱い)");
    Run(now, 3);
    check(Out().running && Out().bps == 32 && Out().capacity == cap16 * 2, "I2Sを32bitスロット・倍のワード数(同じ長さ)で開き直す");

    //24bitのWAVの下位ビットが出力まで残る。音量50%(全体)×100%(WAV)で、左 0x123457×0.5、右 -0x0ABCDF×0.5
    check(WavPlay("/music/k24.wav", false, 100), "24bitで24bitのWAVを鳴らす");
    std::vector<uint32_t> words;
    Run(now, 30, &words);
    size_t pairs = 0, exact = 0;
    const int32_t want_l = (int32_t)(0x123457LL * 5000 / 10000), want_r = (int32_t)(-0x0ABCDFLL * 5000 / 10000);
    for(size_t i = 0; i + 1 < words.size(); i += 2){
        const int32_t l = (int32_t)words[i] >> 8, r = (int32_t)words[i + 1] >> 8;
        if((words[i] & 0xFF) != 0 || (words[i + 1] & 0xFF) != 0) continue;     //上位24bitに入っている
        pairs++;
        if(l == want_l && r == want_r) exact++;
    }
    char buf[200];
    snprintf(buf, sizeof(buf), "左右の組 %zu のうち %zu が24bitの値そのまま(左 %06X 右 %06X)", pairs, exact, want_l, want_r & 0xFFFFFF);
    check(pairs * 2 == words.size() && exact > 1000, buf);
    check((want_l & 0xFF) != 0, "(16bitでは消える下位8bitを含む値で確かめている)");
    WavStop();
    Run(now, 5);

    //効果音(16bitで作る)も24bitの出力で鳴る(8bit上へずらした値)
    words.clear();
    Beep(1000, 50);
    Run(now, 30, &words);
    size_t loud = 0;
    bool aligned = true;
    for(uint32_t w : words){
        const int32_t v = (int32_t)w >> 8;
        if(v > 100000 || v < -100000) loud++;
        aligned = aligned && (v & 0xFF) == 0;
    }
    check(loud > 100 && aligned, "効果音も鳴る(16bitの音を8bit上へずらした値)");

    //sound.cfg の bit-depth
    HostSd::files["/sys/sound.cfg"] = "volume = 50\nsample-rate = 48000\nbit-depth = 16\n";
    SetupAt(now);
    Run(now, 3);
    check(BitDepth() == kBitDepth16 && Out().bps == 16 && Out().capacity == cap16, "bit-depth = 16 で16bitへ戻る");
    HostSd::files["/sys/sound.cfg"] = "volume = 50\nsample-rate = 48000\nbit-depth = 24\n";
    SetupAt(now);
    Run(now, 3);
    check(BitDepth() == kBitDepth24 && Out().bps == 32, "bit-depth = 24 を読む");
    HostSd::files["/sys/sound.cfg"] = "volume = 50\nsample-rate = 48000\nbit-depth = 20\n";
    SetupAt(now);
    Run(now, 3);
    check(BitDepth() == kBitDepth16 && Out().bps == 16, "読めない値は16bit");

    //16bitへ戻せば今までどおり(左右を1ワードに詰める)
    check(WavPlay("/music/k24.wav", false, 100), "16bitで24bitのWAVを鳴らす");
    words.clear();
    Run(now, 30, &words);
    size_t ok16 = 0;
    for(uint32_t w : words) if((int16_t)(w >> 16) == (0x123457 >> 8) * 5000 / 10000) ok16++;
    check(ok16 > 1000, "16bitでは上位16bit(1ワードに左右)");
    WavStop();
    Run(now, 5);
}

int main(){
    TestDecoders();
    TestSoundFunctions();
    printf("\n%s (%d 失敗)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
