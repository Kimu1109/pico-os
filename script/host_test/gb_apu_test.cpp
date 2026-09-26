// ゲームボーイの音源チップの再現(GbApu)と、エミュから時刻付きで渡す列(GbAudioLink)のテスト。
//
//   GbApu      … 矩形波の高さ/デューティ、長さカウンタ、エンベロープ、スイープ(溢れで止まる)、
//                波形メモリ、ノイズ、NR52の電源、NR51の振り分け、全体の音量
//   GbAudioLink … フレームの中の時刻どおりの位置で書き込みが効くこと、次のフレームが来なければ
//                無音になること、溜まりすぎたら追いつくこと、止めたら無音、満杯なら捨てる
#include "sound/Gb_Apu.hpp"
#include "sound/Gb_Audio_Link.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void near(double a, double e, double tol, const char* label){
    const bool ok = std::fabs(a - e) <= tol;
    printf("%s %-50s 実測=%.3f 期待=%.3f±%.3f\n", ok ? "[ OK ]" : "[FAIL]", label, a, e, tol);
    if(!ok) failures++;
}

constexpr uint32_t kRate = 22050;

// 0xFF10からの位置
enum : uint8_t {
    NR10 = 0x00, NR11, NR12, NR13, NR14,
    NR21 = 0x06, NR22, NR23, NR24,
    NR30 = 0x0A, NR31, NR32, NR33, NR34,
    NR41 = 0x10, NR42, NR43, NR44,
    NR50 = 0x14, NR51, NR52,
    WAVE = 0x20,
};

static std::vector<int16_t> Render(GbApu& apu, size_t n){
    std::vector<int16_t> v(n, 0);
    apu.renderAdd(v.data(), n);
    return v;
}

// 負→正へ横切った回数(高さを数える)
static int RisingCrossings(const std::vector<int16_t>& v){
    int n = 0;
    for(size_t i = 1; i < v.size(); i++) if(v[i - 1] < 0 && v[i] >= 0) n++;
    return n;
}

static double Rms(const std::vector<int16_t>& v, size_t from, size_t to){
    double s = 0;
    for(size_t i = from; i < to; i++) s += (double)v[i] * v[i];
    return std::sqrt(s / (double)(to - from));
}

static void PowerOn(GbApu& apu){
    apu.reset();
    apu.write(NR52, 0x80);
    apu.write(NR50, 0x77);
    apu.write(NR51, 0xFF);
}

// ch2を f で鳴らす
static void Ch2(GbApu& apu, uint16_t f, uint8_t nr21, uint8_t nr22, bool length_enable = false){
    apu.write(NR21, nr21);
    apu.write(NR22, nr22);
    apu.write(NR23, (uint8_t)(f & 0xFF));
    apu.write(NR24, (uint8_t)(0x80 | (length_enable ? 0x40 : 0) | (f >> 8)));
}

static void testPulse(){
    printf("--- 矩形波\n");
    GbApu apu(kRate);
    PowerOn(apu);
    // 131072/(2048-1750) = 439.8Hz
    Ch2(apu, 1750, 0x80, 0xF0);
    check(apu.activeMask() == 0x02, "トリガーでch2が鳴る");
    const auto v = Render(apu, kRate);
    near(RisingCrossings(v), 439.8, 3, "1秒の周期の数 = 131072/(2048-f)");

    // デューティ: 高い側のサンプルの割合(直流を落としているので0より上を数える)
    const double expect[4] = { 0.125, 0.25, 0.5, 0.75 };
    const char* names[4] = { "デューティ12.5%", "デューティ25%", "デューティ50%", "デューティ75%" };
    for(int d = 0; d < 4; d++){
        PowerOn(apu);
        Ch2(apu, 1750, (uint8_t)(d << 6), 0xF0);
        const auto w = Render(apu, kRate);
        int16_t hi = -32768, lo = 32767;
        for(size_t i = kRate / 2; i < w.size(); i++){ if(w[i] > hi) hi = w[i]; if(w[i] < lo) lo = w[i]; }
        const int mid = (hi + lo) / 2;
        size_t high = 0;
        for(size_t i = kRate / 2; i < w.size(); i++) if(w[i] > mid) high++;
        near((double)high / (double)(w.size() - kRate / 2), expect[d], 0.03, names[d]);
    }
}

static void testLengthAndEnvelope(){
    printf("--- 長さ・エンベロープ\n");
    GbApu apu(kRate);
    PowerOn(apu);
    // 長さ 64-0 = 64/256秒 = 0.25秒
    Ch2(apu, 1750, 0x80, 0xF0, true);
    Render(apu, (size_t)(kRate * 0.24));
    check(apu.activeMask() == 0x02, "0.24秒ではまだ鳴っている");
    Render(apu, (size_t)(kRate * 0.02));
    check(apu.activeMask() == 0, "0.26秒で長さが尽きて止まる");

    // 長さを有効にしなければ止まらない
    PowerOn(apu);
    Ch2(apu, 1750, 0x80, 0xF0, false);
    Render(apu, kRate);
    check(apu.activeMask() == 0x02, "長さが無効なら鳴り続ける");

    // エンベロープ: 15から1/64秒ごとに1段下げる → 約0.23秒で0(チャンネルは止まらないが無音)
    PowerOn(apu);
    Ch2(apu, 1750, 0x80, 0xF1);
    const auto v = Render(apu, (size_t)(kRate * 0.5));
    const double early = Rms(v, 200, 1200), late = Rms(v, (size_t)(kRate * 0.3), (size_t)(kRate * 0.5));
    check(early > 3000, "エンベロープの始めは大きい");
    check(late < 300, "15段下げ終わると聞こえない");
    check(apu.activeMask() == 0x02, "音量0でもチャンネルは止まらない(実機どおり)");

    // DACを切る(NRx2の上位5bitが0)と止まる
    apu.write(NR22, 0x00);
    check(apu.activeMask() == 0, "DACを切ると止まる");
    Ch2(apu, 1750, 0x80, 0x00);
    check(apu.activeMask() == 0, "DACが切れているとトリガーしても鳴らない");
}

static void testSweep(){
    printf("--- スイープ(ch1)\n");
    GbApu apu(kRate);
    PowerOn(apu);
    // 周期1(1/128秒ごと)、上げる、shift 1: f += f/2 → 1024 → 1536 → 2304(>2047で止まる)
    apu.write(NR10, 0x11);
    apu.write(NR11, 0x80);
    apu.write(NR12, 0xF0);
    apu.write(NR13, 0x00);
    apu.write(NR14, 0x84);   // f = 0x400 = 1024
    check(apu.activeMask() == 0x01, "ch1が鳴る");
    Render(apu, kRate / 10);
    check(apu.activeMask() == 0, "周波数が溢れたら止まる");

    // 下げる向きは溢れない
    PowerOn(apu);
    apu.write(NR10, 0x19);
    apu.write(NR11, 0x80);
    apu.write(NR12, 0xF0);
    apu.write(NR13, 0x00);
    apu.write(NR14, 0x84);
    Render(apu, kRate / 2);
    check(apu.activeMask() == 0x01, "下げる向きでは鳴り続ける");
}

static void testWaveAndNoise(){
    printf("--- 波形メモリ・ノイズ\n");
    GbApu apu(kRate);
    PowerOn(apu);
    // のこぎり波 0,1,...,15,0,1,...,15
    for(int i = 0; i < 16; i++) apu.write((uint8_t)(WAVE + i), (uint8_t)(((i * 2) % 16) << 4 | ((i * 2 + 1) % 16)));
    apu.write(NR30, 0x80);
    apu.write(NR32, 0x20);   // 100%
    // 65536/(2048-f) Hz。f = 2048-149 = 1899 → 439.8Hz。32段のうち16段で1周期なので880Hz弱
    apu.write(NR33, (uint8_t)(1899 & 0xFF));
    apu.write(NR34, (uint8_t)(0x80 | (1899 >> 8)));
    check(apu.activeMask() == 0x04, "ch3が鳴る");
    auto v = Render(apu, kRate);
    near(RisingCrossings(v), 879.6, 6, "波形メモリの周期(1周に2つの山)");
    const double loud = Rms(v, kRate / 2, kRate);
    apu.write(NR32, 0x60);   // 25%
    v = Render(apu, kRate);
    near(Rms(v, kRate / 2, kRate) / loud, 0.25, 0.05, "NR32=25%で振幅が1/4");
    apu.write(NR30, 0x00);
    check(apu.activeMask() == 0, "NR30のDACを切ると止まる");

    PowerOn(apu);
    apu.write(NR42, 0xF0);
    apu.write(NR43, 0x20);
    apu.write(NR44, 0x80);
    v = Render(apu, kRate / 4);
    check(apu.activeMask() == 0x08, "ch4が鳴る");
    check(Rms(v, 1000, v.size()) > 2000, "ノイズが出る");
    apu.write(NR43, 0xF0);   // shift 15: LFSRが進まない
    v = Render(apu, kRate / 2);
    check(Rms(v, kRate / 4, v.size()) < 200, "shift 14以上ではLFSRが止まり、直流なので消える");
}

static void testMixer(){
    printf("--- 電源・振り分け・音量\n");
    GbApu apu(kRate);
    PowerOn(apu);
    Ch2(apu, 1750, 0x80, 0xF0);
    const double full = Rms(Render(apu, kRate / 2), kRate / 4, kRate / 2);
    check(full > 3000 && full < 12000, "ch2だけ全開の大きさ(1チャンネルの最大の範囲内)");

    apu.write(NR51, 0x20);   // ch2を左だけ
    near(Rms(Render(apu, kRate / 2), kRate / 4, kRate / 2) / full, 0.5, 0.05, "片側だけなら半分");
    apu.write(NR51, 0x00);
    check(Rms(Render(apu, kRate / 2), kRate / 4, kRate / 2) < 100, "NR51で振り分けなければ無音");
    apu.write(NR51, 0xFF);
    apu.write(NR50, 0x33);   // 左右とも 3+1 = 4/8
    near(Rms(Render(apu, kRate / 2), kRate / 4, kRate / 2) / full, 0.5, 0.05, "NR50=3で半分");
    apu.write(NR50, 0x77);
    apu.setMasterVolume(0);
    check(Rms(Render(apu, kRate / 2), 0, kRate / 2) == 0, "全体の音量0で無音");
    apu.setMasterVolume(100);

    apu.write(NR52, 0x00);
    check(!apu.powered() && apu.activeMask() == 0, "電源を切ると全部止まる");
    Ch2(apu, 1750, 0x80, 0xF0);
    check(apu.activeMask() == 0, "電源が切れている間は書き込みを受け付けない");
    apu.write(NR52, 0x80);
    Ch2(apu, 1750, 0x80, 0xF0);
    check(apu.activeMask() == 0x02, "電源を入れ直せば鳴る");

    // 4チャンネル全開でも16bitに収まる(頭打ちしない)
    PowerOn(apu);
    for(int i = 0; i < 16; i++) apu.write((uint8_t)(WAVE + i), i < 8 ? 0xFF : 0x00);
    apu.write(NR11, 0x80); apu.write(NR12, 0xF0); apu.write(NR13, 0x00); apu.write(NR14, 0x87);
    Ch2(apu, 1750, 0x80, 0xF0);
    apu.write(NR30, 0x80); apu.write(NR32, 0x20); apu.write(NR33, 0x00); apu.write(NR34, 0x87);
    apu.write(NR42, 0xF0); apu.write(NR43, 0x20); apu.write(NR44, 0x80);
    const auto v = Render(apu, kRate);
    int peak = 0;
    for(auto s : v) if(std::abs((int)s) > peak) peak = std::abs((int)s);
    check(peak < 32767, "4チャンネル全開でも頭打ちしない");
}

// ---- 列 ----

static void testLink(){
    printf("--- 時刻付きの列(GbAudioLink)\n");
    GbApu apu(kRate);
    auto* link = new GbAudioLink(kRate);

    // 何も積んでいなければ何も作らない
    std::vector<int16_t> buf(4000, 0);
    link->render(apu, buf.data(), buf.size());
    check(!link->active(), "始める前は動かない");

    // 始めて、1フレーム目のちょうど半分(35112クロック)でch2をトリガーする
    link->begin();
    link->write(0, NR50, 0x77);
    link->write(0, NR51, 0xFF);
    link->write(35112, NR21, 0x80);
    link->write(35112, NR22, 0xF0);
    link->write(35112, NR23, (uint8_t)(1750 & 0xFF));
    link->write(35112, NR24, (uint8_t)(0x80 | (1750 >> 8)));
    link->endFrame();
    // 2フレーム目は何もしない
    link->endFrame();

    buf.assign(369 * 2, 0);
    link->render(apu, buf.data(), buf.size());
    check(link->active(), "始める");
    // フレームは369.19サンプル。半分 = 184.6 → 184か185サンプル目から鳴る
    size_t first = buf.size();
    for(size_t i = 0; i < buf.size(); i++) if(buf[i] != 0){ first = i; break; }
    near((double)first, 184.6, 1.0, "フレームの中の時刻どおりの位置で鳴り始める");

    // 次のフレームが来なければ、50ms(1102サンプル)で無音になる
    buf.assign(3000, 0);
    link->render(apu, buf.data(), buf.size());
    bool sounding_early = false, silent_late = true;
    for(size_t i = 0; i < 800; i++) if(buf[i] != 0) sounding_early = true;
    for(size_t i = 1200; i < buf.size(); i++) if(buf[i] != 0) silent_late = false;
    check(sounding_early, "少し遅れても鳴り続ける(1コア目の揺れを吸収)");
    check(silent_late, "エミュが止まったら無音(最後の音を鳴らし続けない)");

    // 溜まりすぎたら音を作らずに追いつく
    for(int f = 0; f < 10; f++) link->endFrame();
    link->write(0, NR22, 0x00);      // 11フレーム目の頭でch2を止める
    link->endFrame();
    // 溜まった11フレームのうち、古い分は音を作らずに当てる。残りの4フレームぶん(約1477サンプル)で追いつく
    buf.assign(369 * 6, 0);
    link->render(apu, buf.data(), buf.size());
    size_t last = 0;
    for(size_t i = 0; i < buf.size(); i++) if(std::abs((int)buf[i]) > 1500) last = i;   // 止めた後の直流の尾は数えない
    check(buf[10] != 0, "追いつく間も鳴っている");
    // 溜まりが4フレームになるまでは音を作らない → 鳴るのは止める直前の3フレームぶん(直流の尾で少し伸びる)
    near((double)last, 369.2 * 3, 150, "溜まった分を全部は鳴らさず、最後の4フレームで追いつく");

    // 止めたら無音
    link->end();
    buf.assign(2000, 0);
    link->render(apu, buf.data(), buf.size());
    bool silent = true;
    for(auto s : buf) if(s != 0) silent = false;
    check(silent && !link->active(), "end()で無音になる");

    // 満杯なら書き込みを捨て、区切りは崩さない
    link->begin();
    uint32_t ok = 0;
    for(uint32_t i = 0; i < GbAudioLink::kRingSize + 10; i++) ok += link->write(i % 70000, NR50, 0x77) ? 1 : 0;
    link->endFrame();
    check(link->dropped() > 0, "満杯なら捨てる");
    check(ok + 2 <= GbAudioLink::kRingSize, "区切りの分は空けておく");
    link->render(apu, nullptr, 369 * 3);
    check(link->active(), "満杯でも区切りまで読める");
    delete link;
}

int main(){
    testPulse();
    testLengthAndEnvelope();
    testSweep();
    testWaveAndNoise();
    testMixer();
    testLink();
    printf("\n%s (失敗 %d)\n", failures ? "gb_apu_test: 失敗あり" : "gb_apu_test: 全部OK", failures);
    return failures ? 1 : 0;
}
