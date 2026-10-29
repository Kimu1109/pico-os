// MP3の再生の検証。
//
// 前半は Mp3Decoder(見出し・タグの読み飛ばし・長さ・左右・周波数の変換・シーク・ループ・断り方・壊れたファイル)。
// .mp3 は mp3_test_data.hpp(gen_mp3_test_data.py が ffmpeg で作ったもの)を stubs/SdFat.h の HostSd::files へ置く。
// 音の中身は正弦波の周波数(上向きのゼロ交差の数)と振幅(RMS)で見る(エンコーダの版でバイト列が変わっても通るように)。
//
// 後半は SoundFunctions の配線(WavPlay が .mp3 を Mp3Decoder で読む・止めると返す・.wav へ切り替えられる)。
// wav_test と同じく、1コア目(UpdateAt)と2コア目(Core1StepAt)を1本のスレッドで交互に呼ぶ。
#include "hal/Sound_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "sound/Mp3_Decoder.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"
#include "mp3_test_data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cmath>
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

template <size_t N>
static std::string Bytes(const unsigned char (&a)[N]){ return std::string((const char*)a, N); }

// 終わりまで読む(lanes が2なら左右交互)。limit フレームで打ち切る
static std::vector<int16_t> ReadAll(Mp3Decoder& dec, size_t limit = 400000){
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

// lane 番目のチャンネルだけを取り出す
static std::vector<int16_t> Lane(const std::vector<int16_t>& v, int lanes, int lane){
    std::vector<int16_t> out;
    for(size_t i = lane; i < v.size(); i += lanes) out.push_back(v[i]);
    return out;
}

static double Rms(const std::vector<int16_t>& v, size_t from, size_t to){
    if(to > v.size()) to = v.size();
    if(from >= to) return 0;
    double sum = 0;
    for(size_t i = from; i < to; i++) sum += (double)v[i] * v[i];
    return sqrt(sum / (double)(to - from));
}

// 上向きのゼロ交差の数から周波数を出す
static double Freq(const std::vector<int16_t>& v, size_t from, size_t to, uint32_t rate){
    if(to > v.size()) to = v.size();
    if(from + 1 >= to) return 0;
    int ups = 0;
    for(size_t i = from + 1; i < to; i++) if(v[i - 1] < 0 && v[i] >= 0) ups++;
    return (double)ups * rate / (double)(to - from);
}

static bool Near(double x, double want, double tol){ return fabs(x - want) <= tol; }

static void TestDecoder(){
    OSData::SD_usable = true;
    HostSd::files["/st.mp3"] = Bytes(kMp3CbrStereo);
    HostSd::files["/tag.mp3"] = Bytes(kMp3TaggedMono);
    HostSd::files["/vbr.mp3"] = Bytes(kMp3Vbr22k);

    check(minimp3_pico_scratch_bytes <= Mp3Decoder::kScratchBytes, "minimp3の作業場所が Mp3Decoder の中に収まる");
    printf("       作業場所 %zuB / 係全体 %zuB\n", minimp3_pico_scratch_bytes, sizeof(Mp3Decoder));

    printf("--- CBR・ステレオ・Info ---\n");
    {
        Mp3Decoder dec;
        dec.setStereoOutput(true);
        check(dec.open("/st.mp3", 44100), "開ける");
        check(dec.channels() == 2 && dec.sampleRate() == 44100 && dec.outChannels() == 2 && dec.bitsPerSample() == 16, "見出し(2ch・44.1kHz)");
        check(dec.bitrateKbps() == 32, "ビットレート");
        check(dec.hasToc(), "InfoのTOCを読む");
        printf("       長さ %ums\n", (unsigned)dec.durationMs());
        check(dec.durationMs() >= 1000 && dec.durationMs() <= 1100, "長さはInfoのフレーム数から(約1秒)");
        const auto all = ReadAll(dec);
        const auto l = Lane(all, 2, 0), r = Lane(all, 2, 1);
        printf("       %zuフレーム 左RMS %.0f 右RMS %.0f 左の周波数 %.1fHz\n", l.size(), Rms(l, 4000, 40000), Rms(r, 4000, 40000), Freq(l, 4000, 40000, 44100));
        check(dec.finished(), "終わりまで読める");
        check(l.size() >= 44100 && l.size() <= 47000, "フレーム数は長さどおり(Infoフレームは鳴らさない)");
        check(Rms(l, 4000, 40000) > 1000, "左は鳴る");
        check(Rms(r, 4000, 40000) < 100, "右は無音のまま(左右が混ざらない)");
        check(Near(Freq(l, 4000, 40000, 44100), 440, 5), "左は440Hz");
        //Infoフレームを鳴らすと頭に1152サンプルの無音が足されるので、エンコーダの遅延(約1100)+α以内に鳴り始める
        size_t first = 0;
        while(first < l.size() && abs(l[first]) < 200) first++;
        printf("       鳴り始め %zuサンプル目\n", first);
        check(first < 2000, "頭の無音はエンコーダの遅延だけ");
    }
    {
        Mp3Decoder dec;     //既定はモノラル(左右の平均)
        check(dec.open("/st.mp3", 44100) && dec.outChannels() == 1, "既定はモノラル出力");
        std::vector<int16_t> st;
        {
            Mp3Decoder s; s.setStereoOutput(true); s.open("/st.mp3", 44100);
            st = Lane(ReadAll(s), 2, 0);
        }
        const auto mono = ReadAll(dec);
        bool ok = mono.size() == st.size();
        for(size_t i = 0; ok && i < mono.size(); i++) ok = abs(mono[i] - st[i] / 2) <= 51;  //右(ほぼ無音)との平均
        check(ok, "モノラルは左右の平均");
    }
    {
        Mp3Decoder dec;
        check(dec.open("/st.mp3", 22050), "22050Hzへ下げて開ける");
        const auto all = ReadAll(dec);
        printf("       %zuサンプル 周波数 %.1fHz\n", all.size(), Freq(all, 2000, 20000, 22050));
        check(all.size() >= 22050 && all.size() <= 23500, "数は半分");
        check(Near(Freq(all, 2000, 20000, 22050), 440, 5), "高さは変わらない");
    }

    printf("--- タグ付き・Xing無し ---\n");
    {
        Mp3Decoder dec;
        check(dec.open("/tag.mp3", 44100), "ID3v2の後のフレームを見つける");
        check(dec.channels() == 1 && !dec.hasToc(), "1ch・TOC無し");
        printf("       長さ %ums\n", (unsigned)dec.durationMs());
        //ID3を長さに数えると1秒を大きく超える
        check(dec.durationMs() >= 1000 && dec.durationMs() <= 1100, "長さはビットレートから(タグを数えない)");
        const auto all = ReadAll(dec);
        printf("       %zuサンプル RMS %.0f 周波数 %.1fHz\n", all.size(), Rms(all, 4000, 40000), Freq(all, 4000, 40000, 44100));
        check(all.size() >= 44100 && all.size() <= 48000, "フレーム数");
        check(Near(Freq(all, 4000, 40000, 44100), 1000, 8), "1kHz");
        check(Rms(all, 4000, 40000) > 1000, "鳴る");
        check(Rms(all, all.size() - 200, all.size()) < 3000, "終わりにID3v1を音として鳴らさない");
    }

    printf("--- MPEG-2 VBR・シーク ---\n");
    {
        Mp3Decoder dec;
        check(dec.open("/vbr.mp3", 22050), "開ける");
        check(dec.sampleRate() == 22050 && dec.hasToc(), "22.05kHz・XingのTOC");
        printf("       長さ %ums\n", (unsigned)dec.durationMs());
        check(dec.durationMs() >= 3000 && dec.durationMs() <= 3150, "長さはXingのフレーム数×576から(約3秒)");
        int16_t buf[4410];
        check(dec.seekMs(2200), "2.2秒へ飛ぶ");
        size_t n = 0;
        while(n < 4410){ const size_t k = dec.read(buf + n, 4410 - n); if(k == 0) break; n += k; }
        std::vector<int16_t> v(buf, buf + n);
        printf("       飛んだ先 %.1fHz\n", Freq(v, 1500, n, 22050));
        check(Near(Freq(v, 1500, n, 22050), 1500, 20), "後ろの半分(1500Hz)が鳴る");
        check(dec.seekMs(300), "0.3秒へ戻る");
        n = 0;
        while(n < 4410){ const size_t k = dec.read(buf + n, 4410 - n); if(k == 0) break; n += k; }
        v.assign(buf, buf + n);
        printf("       戻った先 %.1fHz\n", Freq(v, 1500, n, 22050));
        check(Near(Freq(v, 1500, n, 22050), 440, 10), "頭の半分(440Hz)が鳴る");
        check(dec.seekMs(99999) && !dec.finished(), "長さより先は終わりぎわへ丸める");
        ReadAll(dec);
        check(dec.finished(), "そのまま終わる");
        dec.close();
        check(!dec.isOpen() && dec.seekMs(1000) && dec.isOpen(), "閉じた後でも開き直して飛べる");
    }
    {
        Mp3Decoder dec;
        check(dec.open("/vbr.mp3", 44100), "22.05kHzを44.1kHzへ上げて開ける");
        const auto all = ReadAll(dec);
        printf("       %zuサンプル\n", all.size());
        check(all.size() >= 132300 && all.size() <= 139000, "数は倍");
        check(Near(Freq(all, 4000, 50000, 44100), 440, 10), "高さは変わらない");
    }

    printf("--- ループ ---\n");
    {
        Mp3Decoder dec;
        dec.setLoop(true);
        check(dec.open("/tag.mp3", 44100), "開ける");
        const auto all = ReadAll(dec, 44100 * 3);
        check(!dec.finished() && all.size() == 44100u * 3, "ループなら終わらない(3秒ぶん読める)");
        check(Rms(all, 44100 * 2 + 4000, 44100 * 2 + 30000) > 1000, "3周目も鳴る");
    }

    printf("--- 断り方 ---\n");
    {
        Mp3Decoder dec;
        HostSd::files["/bad.mp3"] = "hello, this is not an mp3 file";
        check(!dec.open("/bad.mp3", 22050) && dec.error() == Mp3Decoder::Error::NotMp3, "MP3でなければNotMp3");
        check(!dec.isOpen() && dec.finished() && dec.read(nullptr, 0) == 0, "失敗したら閉じている");
        HostSd::files["/zero.mp3"] = std::string(100000, '\0');
        check(!dec.open("/zero.mp3", 22050) && dec.error() == Mp3Decoder::Error::NotMp3, "0ばかり(64KB以上)はNotMp3");
        HostSd::files["/empty.mp3"] = "";
        check(!dec.open("/empty.mp3", 22050) && dec.error() == Mp3Decoder::Error::NotMp3, "空はNotMp3");
        //ID3v2だけ(大きさ 100 の空のタグ)
        std::string id3 = "ID3";
        id3 += (char)3; id3 += (char)0; id3 += (char)0;
        id3 += (char)0; id3 += (char)0; id3 += (char)0; id3 += (char)100;
        id3.append(100, '\0');
        HostSd::files["/id3only.mp3"] = id3;
        check(!dec.open("/id3only.mp3", 22050) && dec.error() == Mp3Decoder::Error::NotMp3, "タグだけならNotMp3");
        check(!dec.open("/nothing.mp3", 22050) && dec.error() == Mp3Decoder::Error::OpenFailed, "無いファイルはOpenFailed");
        OSData::SD_usable = false;
        check(!dec.open("/st.mp3", 22050) && dec.error() == Mp3Decoder::Error::NoSd, "SDが無ければNoSd");
        OSData::SD_usable = true;
    }

    printf("--- 壊れたファイル ---\n");
    {
        //途中で切れている
        HostSd::files["/cut.mp3"] = Bytes(kMp3CbrStereo).substr(0, sizeof(kMp3CbrStereo) / 2 + 37);
        Mp3Decoder dec;
        check(dec.open("/cut.mp3", 22050), "途中で切れていても開ける");
        const auto all = ReadAll(dec);
        printf("       %zuサンプル\n", all.size());
        check(dec.finished() && all.size() > 5000 && all.size() < 15000, "切れた所まで鳴って終わる");

        //ところどころを書き換えたもの(ASanで範囲外を踏まないか・終わるか)
        uint32_t seed = 12345;
        auto rnd = [&]{ seed = seed * 1103515245u + 12345u; return seed >> 8; };
        const std::string base[] = {Bytes(kMp3CbrStereo), Bytes(kMp3TaggedMono), Bytes(kMp3Vbr22k)};
        int opened = 0, done = 0;
        for(int t = 0; t < 60; t++){
            std::string f = base[t % 3];
            const int edits = 1 + (int)(rnd() % 40);
            for(int e = 0; e < edits; e++) f[rnd() % f.size()] = (char)(rnd() & 0xFF);
            HostSd::files["/fuzz.mp3"] = f;
            Mp3Decoder d;
            d.setStereoOutput(t & 1);
            d.setLoop(t % 5 == 0);
            if(!d.open("/fuzz.mp3", (t & 2) ? 44100 : 22050)) continue;
            opened++;
            ReadAll(d, 200000);
            if(d.seekMs(rnd() % 4000)) ReadAll(d, 50000);
            done++;
        }
        printf("       開けた %d / 60\n", opened);
        check(done == opened, "書き換えたファイルでも最後まで読める");
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

static void TestSoundFunctions(){
    using namespace SoundFunctions;
    printf("\n===== SoundFunctions =====\n");
    HostGpio::read_hook = &ReadHook;
    static unsigned long now = 1000;
    SetupAt(now);
    check(SetSampleRate(kSampleRateHigh), "44100Hzで鳴らす");
    Step(now);
    for(int i = 0; i < 3; i++){ now += 1; Step(now); }
    check(Out().running, "I2Sが動く");
    HostSd::files["/music/st.mp3"] = Bytes(kMp3CbrStereo);
    HostSd::files["/music/TAG.MP3"] = Bytes(kMp3TaggedMono);
    HostSd::files["/music/bad.mp3"] = "not an mp3";

    printf("--- 鳴らす ---\n");
    {
        const char* err = nullptr;
        WavInfo info;
        check(WavPlay("/music/st.mp3", false, 100, &err, &info), "MP3を鳴らせる");
        check(info.channels == 2 && info.sample_rate == 44100 && info.bits == 16 && info.duration_ms >= 1000 && info.duration_ms <= 1100, "情報を返す");
        check(WavDurationMs() == info.duration_ms && strcmp(WavTitle(), "st.mp3") == 0, "長さと名前");
        check(WavPlaying(), "鳴らした直後から鳴っている扱い");
        std::vector<int16_t> l, r;
        Run(now, 400, &l, &r);
        printf("       %zuフレーム 左RMS %.0f 右RMS %.0f\n", l.size(), Rms(l, 4000, 40000), Rms(r, 4000, 40000));
        check(!WavPlaying(), "鳴り終えたら止まる");
        check(Rms(l, 4000, 40000) > 300 && Rms(r, 4000, 40000) < 60, "左だけ鳴る(左右を残す)");
        check(Near(Freq(l, 4000, 40000, 44100), 440, 5), "440Hz");
    }

    printf("--- シーク・一時停止 ---\n");
    {
        check(WavPlay("/music/TAG.MP3", false, 100), "拡張子の大小は区別しない");
        Run(now, 3);
        check(WavSeekMs(500), "飛べる");
        const uint32_t pos = WavPositionMs();
        printf("       位置 %ums\n", (unsigned)pos);
        check(pos >= 400 && pos <= 600, "位置は飛んだ先");
        check(WavPause(true) && WavPaused(), "止められる");
        WavStop();
        check(!WavPlaying() && !WavSeekMs(100), "止めたら飛べない");
    }

    printf("--- 読めないMP3・WAVへの切り替え ---\n");
    {
        const char* err = nullptr;
        check(!WavPlay("/music/bad.mp3", false, 100, &err) && err && strstr(err, "MP3") != nullptr, "読めなければ理由を返す");
        //WAVを鳴らすとMP3の係は返し、WAVの係で読む
        std::string wav = "RIFF____WAVEfmt ";
        auto put16 = [&](uint32_t v){ wav += (char)(v & 0xFF); wav += (char)(v >> 8); };
        auto put32 = [&](uint32_t v){ put16(v & 0xFFFF); put16(v >> 16); };
        put32(16); put16(1); put16(1); put32(44100); put32(88200); put16(2); put16(16);
        wav += "data"; put32(2000);
        for(int i = 0; i < 1000; i++) put16(6000);
        HostSd::files["/music/c.wav"] = wav;
        check(WavPlay("/music/c.wav", false, 100), "続けてWAVを鳴らせる");
        std::vector<int16_t> l;
        Run(now, 50, &l);
        size_t n = 0;
        for(int16_t s : l) if(s == 3000) n++;
        check(n == 1000, "WAVはそのまま鳴る(全体の音量50)");
        check(WavPlay("/music/st.mp3", false, 100), "WAVの後にもう一度MP3を鳴らせる");
        Run(now, 400);
        check(!WavPlaying(), "鳴り終える");
    }
}

int main(){
    TestDecoder();
    TestSoundFunctions();
    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
