// WAVの再生の検証。
//
// 前半は WavDecoder(見出しの読み方・形式ごとの変換・チャンネルの平均・周波数の変換・ループ・断り方)。
// .wav はテストの中で組み立てて stubs/SdFat.h の HostSd::files へ置く。
//
// 後半は SoundFunctions の配線(WavPlay/WavStop/WavPlaying)。sound_test と同じく、1コア目(UpdateAt)と
// 2コア目(Core1StepAt)を1本のスレッドで交互に呼び、I2Sへ書かれた値を見る。
#include "functions/Sound_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "sound/Wav_Decoder.hpp"
#include "sound/Wav_Stream.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cstdio>
#include <cstring>
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

// ================================================================
// .wavを組み立てる
// ================================================================

static void Put16(std::string& s, uint32_t v){ s += (char)(v & 0xFF); s += (char)((v >> 8) & 0xFF); }
static void Put32(std::string& s, uint32_t v){ Put16(s, v & 0xFFFF); Put16(s, v >> 16); }
static void Chunk(std::string& s, const char* id, const std::string& body){
    s.append(id, 4);
    Put32(s, (uint32_t)body.size());
    s += body;
    if(body.size() & 1) s += '\0';
}

struct WavSpec {
    uint16_t format = 1;
    uint16_t channels = 1;
    uint32_t rate = 22050;
    uint16_t bits = 16;
    bool extensible = false;
    std::string before_fmt;     // fmtより前に挟むチャンク
    std::string before_data;    // fmtとdataの間に挟むチャンク
    int64_t data_size_override = -1;
};

static std::string Fmt(const WavSpec& w){
    std::string f;
    Put16(f, w.extensible ? 0xFFFE : w.format);
    Put16(f, w.channels);
    Put32(f, w.rate);
    Put32(f, w.rate * w.channels * (w.bits / 8));
    Put16(f, w.channels * (w.bits / 8));
    Put16(f, w.bits);
    if(w.extensible){
        Put16(f, 22);               // cbSize
        Put16(f, w.bits);           // wValidBitsPerSample
        Put32(f, 0);                // dwChannelMask
        Put16(f, w.format);         // サブフォーマットのGUIDの頭
        f.append(14, '\x01');
    }
    return f;
}

static std::string MakeWav(const WavSpec& w, const std::string& data){
    std::string body = "WAVE";
    body += w.before_fmt;
    Chunk(body, "fmt ", Fmt(w));
    body += w.before_data;
    if(w.data_size_override >= 0){
        body.append("data", 4);
        Put32(body, (uint32_t)w.data_size_override);
        body += data;
    }else{
        Chunk(body, "data", data);
    }
    std::string s = "RIFF";
    Put32(s, (uint32_t)body.size());
    return s + body;
}

static std::string Pcm16(const std::vector<int16_t>& v){
    std::string d;
    for(int16_t x : v) Put16(d, (uint16_t)x);
    return d;
}

// 全部読む
static std::vector<int16_t> ReadAll(WavDecoder& dec, size_t limit = 100000){
    std::vector<int16_t> out;
    int16_t buf[37];    // わざと半端な大きさで読む
    while(!dec.finished() && out.size() < limit){
        const size_t n = dec.read(buf, sizeof(buf) / sizeof(buf[0]));
        out.insert(out.end(), buf, buf + n);
        if(n == 0) break;
    }
    return out;
}

static std::vector<int16_t> Decode(const std::string& file, uint32_t out_rate = 22050){
    HostSd::files["/t.wav"] = file;
    WavDecoder dec;
    if(!dec.open("/t.wav", out_rate)) return {};
    return ReadAll(dec);
}

static void TestDecoder(){
    OSData::SD_usable = true;

    printf("--- 16bitモノラル・同じ周波数はそのまま ---\n");
    {
        std::vector<int16_t> src;
        for(int i = 0; i < 1000; i++) src.push_back((int16_t)(i * 37 - 18000));
        const auto out = Decode(MakeWav(WavSpec{}, Pcm16(src)));
        check(out == src, "入れたのと同じ数・同じ値が出る(最後の1つも)");

        HostSd::files["/t.wav"] = MakeWav(WavSpec{}, Pcm16(src));
        WavDecoder dec;
        dec.open("/t.wav", 22050);
        check(dec.channels() == 1 && dec.bitsPerSample() == 16 && dec.sampleRate() == 22050, "見出しを読める");
        check(dec.durationMs() == 45, "長さ(1000/22050秒 ≒ 45ms)");
    }

    printf("--- 形式ごとの変換 ---\n");
    {
        WavSpec w; w.bits = 8;
        std::string d; d += (char)128; d += (char)255; d += (char)0;
        const auto out = Decode(MakeWav(w, d));
        check(out.size() == 3 && out[0] == 0 && out[1] == 32512 && out[2] == -32768, "8bitは符号なし(128が無音)");
    }
    {
        WavSpec w; w.bits = 24;
        std::string d;
        d += '\x00'; Put16(d, 0x1234);      // 0x123400 → 0x1234
        d += '\xFF'; Put16(d, 0x8000);      // 最小
        const auto out = Decode(MakeWav(w, d));
        check(out.size() == 2 && out[0] == 0x1234 && out[1] == -32768, "24bitは上位16bit");
    }
    {
        WavSpec w; w.bits = 32;
        std::string d; Put32(d, 0x7FFF0000u); Put32(d, 0xC0000000u);
        const auto out = Decode(MakeWav(w, d));
        check(out.size() == 2 && out[0] == 32767 && out[1] == -16384, "32bit整数は上位16bit");
    }
    {
        WavSpec w; w.format = 3; w.bits = 32;
        std::string d;
        const float fs[] = {0.5f, -1.0f, 2.0f};
        for(float f : fs){ uint32_t u; memcpy(&u, &f, 4); Put32(d, u); }
        const auto out = Decode(MakeWav(w, d));
        check(out.size() == 3 && out[0] == 16383 && out[1] == -32767 && out[2] == 32767, "32bit浮動小数点(範囲外は頭打ち)");
    }
    {
        WavSpec w; w.channels = 2;
        const auto out = Decode(MakeWav(w, Pcm16({1000, 3000, -500, -1500})));
        check(out.size() == 2 && out[0] == 2000 && out[1] == -1000, "ステレオは左右の平均");
    }
    {
        WavSpec w; w.channels = 6;
        const auto out = Decode(MakeWav(w, Pcm16({600, 600, 600, 0, 0, 0})));
        check(out.size() == 1 && out[0] == 300, "6チャンネルも平均");
    }
    {
        WavSpec w; w.extensible = true; w.channels = 2;
        const auto out = Decode(MakeWav(w, Pcm16({100, 300})));
        check(out.size() == 1 && out[0] == 200, "WAVE_FORMAT_EXTENSIBLE(中身はPCM)");
    }

    printf("--- 周波数の変換 ---\n");
    {
        WavSpec w; w.rate = 44100;
        std::vector<int16_t> src;
        for(int i = 0; i < 100; i++) src.push_back((int16_t)(i * 100));
        const auto out = Decode(MakeWav(w, Pcm16(src)));
        bool ok = out.size() == 50;
        for(size_t i = 0; ok && i < out.size(); i++) ok = (out[i] == src[i * 2]);
        check(ok, "44100Hz→22050Hz は1つおき");
    }
    {
        WavSpec w; w.rate = 11025;
        const auto out = Decode(MakeWav(w, Pcm16({0, 1000, 2000})));
        check(out.size() == 6 && out[0] == 0 && out[1] == 500 && out[2] == 1000 && out[3] == 1500 && out[4] == 2000,
              "11025Hz→22050Hz は間を直線で補う");
    }
    {
        WavSpec w; w.rate = 48000;
        std::vector<int16_t> src(48000, 1234);
        const auto out = Decode(MakeWav(w, Pcm16(src)));
        printf("       48000サンプル→%zu\n", out.size());
        check(out.size() >= 22049 && out.size() <= 22051, "48000Hzの1秒は約22050サンプル");
        bool flat = true;
        for(int16_t v : out) if(v != 1234) flat = false;
        check(flat, "一定の値は一定のまま");
    }

    printf("--- チャンクの読み飛ばし ---\n");
    {
        WavSpec w;
        std::string list; Chunk(list, "LIST", "abc");      // 奇数の長さ(詰め物が入る)
        std::string junk; Chunk(junk, "JUNK", std::string(300, 'x'));
        w.before_fmt = junk;
        w.before_data = list;
        const auto out = Decode(MakeWav(w, Pcm16({7, 8, 9})));
        check(out.size() == 3 && out[0] == 7 && out[2] == 9, "LIST/JUNK(奇数の長さを含む)を飛ばす");
    }
    {
        WavSpec w; w.data_size_override = 0xFFFFFFFF;  // 長さ未記入
        const auto out = Decode(MakeWav(w, Pcm16({1, 2, 3, 4})));
        check(out.size() == 4 && out[3] == 4, "dataの長さがファイルより長ければ終わりまで");
    }
    {
        //半端なバイト(1フレームに満たない)は読まない
        std::string d = Pcm16({5, 6}); d += '\x01';
        WavSpec w;
        const auto out = Decode(MakeWav(w, d));
        check(out.size() == 2, "最後の半端なフレームは読まない");
    }

    printf("--- ループ ---\n");
    {
        HostSd::files["/t.wav"] = MakeWav(WavSpec{}, Pcm16({1, 2, 3}));
        WavDecoder dec;
        dec.open("/t.wav", 22050);
        dec.setLoop(true);
        int16_t buf[8];
        const size_t n = dec.read(buf, 8);
        check(n == 8 && !dec.finished() && buf[0] == 1 && buf[2] == 3 && buf[3] == 1 && buf[7] == 2, "終わりまで来たら頭から");
    }

    printf("--- 断り方 ---\n");
    {
        WavDecoder dec;
        HostSd::files["/t.wav"] = "hello, this is not a wave file";
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::NotWav, "RIFF/WAVEでなければNotWav");
        check(strlen(dec.errorText()) > 0, "理由の文がある");

        WavSpec w; w.format = 2; w.bits = 16;    // MS ADPCM
        HostSd::files["/t.wav"] = MakeWav(w, Pcm16({1, 2}));
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::Unsupported, "ADPCMはUnsupported");

        WavSpec w2; w2.format = 3; w2.bits = 64;
        HostSd::files["/t.wav"] = MakeWav(w2, std::string(16, '\0'));
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::Unsupported, "64bit浮動小数点はUnsupported");

        WavSpec w3; w3.channels = 9;
        HostSd::files["/t.wav"] = MakeWav(w3, std::string(18, '\0'));
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::Unsupported, "9チャンネル以上はUnsupported");

        std::string no_data = "RIFF"; std::string body = "WAVE"; Chunk(body, "fmt ", Fmt(WavSpec{}));
        Put32(no_data, (uint32_t)body.size()); no_data += body;
        HostSd::files["/t.wav"] = no_data;
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::NoData, "dataが無ければNoData");

        HostSd::files["/t.wav"] = MakeWav(WavSpec{}, "");
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::NoData, "dataが空ならNoData");

        check(!dec.open("/nothing.wav", 22050) && dec.error() == WavDecoder::Error::OpenFailed, "無いファイルはOpenFailed");

        OSData::SD_usable = false;
        check(!dec.open("/t.wav", 22050) && dec.error() == WavDecoder::Error::NoSd, "SDが無ければNoSd");
        OSData::SD_usable = true;
        check(!dec.isOpen() && dec.read(nullptr, 0) == 0, "失敗した後は閉じている");
    }
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

static void Step(unsigned long now){
    SoundFunctions::UpdateAt(now);
    SoundFunctions::Core1StepAt(now);
}

// I2Sが全部送ったことにして進める。書かれたサンプルを返す
static std::vector<int16_t> Run(unsigned long& now, int steps, bool core0 = true){
    std::vector<int16_t> got;
    for(int i = 0; i < steps; i++){
        Out().consume(Out().queued.size());
        const size_t before = Out().written.size();
        now += 1;
        if(core0) SoundFunctions::UpdateAt(now);
        SoundFunctions::Core1StepAt(now);
        for(size_t k = before; k < Out().written.size(); k++) got.push_back(Left(Out().written[k]));
    }
    return got;
}

static size_t Count(const std::vector<int16_t>& v, int16_t x){
    size_t n = 0;
    for(int16_t s : v) if(s == x) n++;
    return n;
}

static void TestSoundFunctions(){
    using namespace SoundFunctions;
    HostGpio::read_hook = &ReadHook;
    unsigned long now = 1000;
    SetupAt(now);
    Step(now);
    check(Out().running, "アンプがあればI2Sが動く");
    check(!WavPlaying() && WavUnderruns() == 0, "最初は鳴っていない");

    printf("--- 鳴らす ---\n");
    {
        //1万サンプルの一定値。全体の音量50・WAVの音量100で 10000*50/100 = 5000
        HostSd::files["/music/a.wav"] = MakeWav(WavSpec{}, Pcm16(std::vector<int16_t>(10000, 10000)));
        const char* err = nullptr;
        WavInfo info;
        check(WavPlay("/music/a.wav", false, 100, &err, &info), "鳴らせる");
        check(info.sample_rate == 22050 && info.channels == 1 && info.duration_ms == 453, "情報を返す");
        check(strcmp(WavTitle(), "a.wav") == 0, "ファイル名を覚える");
        check(WavPlaying(), "鳴らした直後から鳴っている扱い");

        const auto got = Run(now, 100);
        printf("       5000の数 %zu\n", Count(got, 5000));
        check(Count(got, 5000) == 10000, "1万サンプルが全体の音量を掛けて鳴る");
        check(!WavPlaying(), "鳴り終えたら止まる");
        check(WavUnderruns() == 0, "途切れていない");
    }

    printf("--- WAVの音量 ---\n");
    {
        WavPlay("/music/a.wav", false, 20);
        const auto got = Run(now, 100);
        check(Count(got, 1000) == 10000, "WAVの音量20なら1000");
    }

    printf("--- 止める ---\n");
    {
        HostSd::files["/music/long.wav"] = MakeWav(WavSpec{}, Pcm16(std::vector<int16_t>(100000, 8000)));
        WavPlay("/music/long.wav", false, 100);
        Run(now, 3);
        WavStop();
        check(!WavPlaying(), "止めたら直後から止まった扱い");
        const auto got = Run(now, 10);
        check(Count(got, 4000) == 0, "先読みしていた分も鳴らさない");
    }

    printf("--- 切り替え ---\n");
    {
        HostSd::files["/music/b.wav"] = MakeWav(WavSpec{}, Pcm16(std::vector<int16_t>(3000, 2000)));
        WavPlay("/music/long.wav", false, 100);
        Run(now, 3);
        WavPlay("/music/b.wav", false, 100);
        const auto got = Run(now, 100);
        check(Count(got, 1000) == 3000, "次のWAVが全部鳴る");
        check(!WavPlaying(), "鳴り終える");
    }

    printf("--- 読めないファイル ---\n");
    {
        HostSd::files["/music/bad.wav"] = "not a wav";
        const char* err = nullptr;
        check(!WavPlay("/music/bad.wav", false, 100, &err) && strlen(err) > 0, "理由を返して断る");
        check(!WavPlaying(), "鳴っていない");
    }

    printf("--- 途切れ ---\n");
    {
        //1コア目(UpdateAt)を回さないと先読み(kWavPrefillSamples)を使い切って途切れる
        WavPlay("/music/long.wav", false, 100);
        Run(now, 40, false);
        check(WavUnderruns() == 1, "途切れを1回と数える");
        Run(now, 40, true);
        check(WavUnderruns() == 1 && WavPlaying(), "1コア目が戻れば続きから鳴る");
        WavStop();
        Run(now, 2);
    }

    printf("--- ループ ---\n");
    {
        WavPlay("/music/b.wav", true, 100);
        const auto got = Run(now, 200);
        check(Count(got, 1000) > 6000 && WavPlaying(), "ループは鳴り続ける");
        WavStop();
    }

    printf("--- 未接続でも時間どおりに進む ---\n");
    {
        plugged = false;
        for(int i = 0; i < kDetectStableCount; i++){ now += kDetectIntervalMs; Step(now); }
        check(!Out().running, "I2Sは止まっている");
        WavPlay("/music/a.wav", false, 100);    // 10000サンプル ≒ 453ms
        now += 200; Step(now);
        check(WavPlaying(), "200msでは鳴り終えない");
        for(int i = 0; i < 10; i++){ now += 100; Step(now); }
        check(!WavPlaying(), "1秒経てば鳴り終えている");
        check(WavUnderruns() == 1, "時間で進めている間も途切れていない");
    }
}

int main(){
    TestDecoder();
    TestSoundFunctions();
    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
