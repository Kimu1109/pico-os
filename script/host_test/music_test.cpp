// 曲データ(pico-os MML、MUSIC_FORMAT.md)の検証。
//
// 1. 読み取り(MmlCompiler): 音符の高さと長さ、繰り返し、マクロ、誤りの位置と理由、警告
// 2. シーケンサー(MusicPlayer): 音源(ChipSynth::Engine)と組み合わせ、音がサンプル単位で
//    正しい位置に鳴ること、繰り返し/ループ/テンポ/マクロの状態の戻し/効果音への貸し出し
// 3. SoundFunctions: 1コア目の MusicPlay*/MusicStop と2コア目(Core1StepAt)の配線、
//    置き場の入れ替え、効果音と曲の同居、SDのファイルから読む
#include "sound/Mml_Compiler.hpp"
#include "sound/Music_Player.hpp"
#include "sound/Chip_Synth.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "consts.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <I2S.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

using namespace MusicData;

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static const uint32_t kRate = 22050;
static uint8_t buf[6144];
static MmlCompiler compiler;

static MmlResult Compile(const std::string& text, size_t cap = sizeof(buf)){
    MmlTextSource src(text.data(), text.size());
    MmlResult r;
    compiler.compile(src, buf, cap, r);
    return r;
}

// 誤りになり、その行・列・理由(の一部)が合っていること
static void ExpectError(const std::string& text, int line, int col, const char* part, const char* label){
    const MmlResult r = Compile(text);
    const bool ok = !r.ok && r.line == line && r.col == col && strstr(r.message.c_str(), part);
    if(!ok) printf("       実際: ok=%d %d行%d列 %s\n", r.ok, r.line, r.col, r.message.c_str());
    check(ok, label);
}

// チャンネルchの命令列を、音符(midi,len)と休符(-1,len)の並びとして取り出す(繰り返しは展開しない)
struct Ev { int midi; int len; };
static std::vector<Ev> Events(int ch){
    std::vector<Ev> v;
    uint16_t pc = ReadU16(buf + 8 + ch * 2);
    if(pc == 0) return v;
    while(buf[pc] != Op::End){
        const uint8_t op = buf[pc++];
        switch(op){
            case Op::Note: v.push_back({buf[pc], ReadU16(buf + pc + 1)}); pc += 3; break;
            case Op::Rest: v.push_back({-1, ReadU16(buf + pc)}); pc += 2; break;
            case Op::Tempo: case Op::LoopBreak: pc += 2; break;
            case Op::Wave: case Op::Volume: case Op::Envelope: case Op::Gate: case Op::LoopBegin: pc += 1; break;
            default: break;
        }
    }
    return v;
}

static bool EventsAre(int ch, std::vector<Ev> expect){
    const auto got = Events(ch);
    bool ok = got.size() == expect.size();
    for(size_t i = 0; ok && i < got.size(); i++) ok = got[i].midi == expect[i].midi && got[i].len == expect[i].len;
    if(!ok){
        printf("       実際:");
        for(const auto& e : got) printf(" (%d,%d)", e.midi, e.len);
        printf("\n");
    }
    return ok;
}

// ================================================================
// 1. 読み取り
// ================================================================
static void TestCompiler(){
    printf("--- 読み取り: 音の高さ ---\n");
    check(Compile("A o4 a").ok && EventsAre(0, {{69, 48}}), "o4 a = 69(440Hz)、長さの既定は4分音符(48)");
    check(Compile("A c d e f g a b").ok && EventsAre(0, {{60,48},{62,48},{64,48},{65,48},{67,48},{69,48},{71,48}}),
          "c〜b(オクターブの既定は4)");
    check(Compile("A c+ c# d- e--").ok && EventsAre(0, {{61,48},{61,48},{61,48},{62,48}}), "+ # で半音上げ、- で下げる(重ねてもよい)");
    check(Compile("A o4 c > c > c < < < c").ok && EventsAre(0, {{60,48},{72,48},{84,48},{48,48}}), "> で上げ < で下げる");
    check(Compile("A k2 c k-12 c").ok && EventsAre(0, {{62,48},{48,48}}), "k で移調");
    check(Compile("A n60 n61,8").ok && EventsAre(0, {{60,48},{61,24}}), "n番号(,長さ)");
    check(Compile("A o0 c o8 b").ok && EventsAre(0, {{12,48},{119,48}}), "o0〜o8");

    printf("--- 読み取り: 長さ ---\n");
    check(Compile("A c1 c2 c8 c16 c32 c64").ok && EventsAre(0, {{60,192},{60,96},{60,24},{60,12},{60,6},{60,3}}), "1〜64分音符");
    check(Compile("A c3 c6 c12 c24").ok && EventsAre(0, {{60,64},{60,32},{60,16},{60,8}}), "3連(3 6 12 24)");
    check(Compile("A c4. c4.. c.").ok && EventsAre(0, {{60,72},{60,84},{60,72}}), "付点(省略した長さにも付く)");
    check(Compile("A c4^8 c^^16 r2^4").ok && EventsAre(0, {{60,72},{60,108},{-1,144}}), "タイ ^(省略すれば l の長さ)");
    check(Compile("A l8 c r c16 l16. d").ok && EventsAre(0, {{60,24},{-1,24},{60,12},{62,18}}), "l で既定の長さ");
    check(Compile("A c | d e | f").ok && EventsAre(0, {{60,48},{62,48},{64,48},{65,48}}), "| と空白は読み飛ばす");
    check(Compile("A E-3 e- q7 v5 @1 t150 e").ok && EventsAre(0, {{63,48},{64,48}}), "E(減衰)と e(音符のミ)を取り違えない");

    printf("--- 読み取り: チャンネルと行 ---\n");
    {
        const MmlResult r = Compile("; 注釈\n#title テスト曲 ; 注釈\n#composer 私\n#tempo 90\n#unknown 何か\n\nA c\nB d\nA e ; 注釈\nCD f\n");
        check(r.ok, "ヘッダ・注釈・空行・知らないヘッダ");
        check(strcmp(r.title.c_str(), "テスト曲") == 0 && strcmp(r.composer.c_str(), "私") == 0, "#title / #composer(注釈は落とす)");
        check(ReadU16(buf + 4) == 90, "#tempo");
        check(EventsAre(0, {{60,48},{64,48}}) && EventsAre(1, {{62,48}}), "同じチャンネルの行はつながる");
        check(EventsAre(2, {{65,48}}) && EventsAre(3, {{65,48}}), "CD は両方へ同じ内容");
    }
    {
        Compile("A c");
        check(ReadU16(buf + 4) == 120 && ReadU16(buf + 10) == 0, "テンポの既定は120、使わないチャンネルは空(0)");
    }

    printf("--- 読み取り: 繰り返し・マクロ ---\n");
    {
        check(Compile("A [c d]3 [e : f]").ok, "[ ] と [ : ] を読める");
        //繰り返しは展開しない
        check(EventsAre(0, {{60,48},{62,48},{64,48},{65,48}}), "繰り返しは展開せずに持つ");
    }
    check(Compile("#macro x = v5 o6 c\nA o4 $x c").ok && EventsAre(0, {{84,48},{60,48}}),
          "マクロの中で変えたオクターブは抜けると戻る");
    check(Compile("#macro k = @noise c16\nD [$k $k]4").ok, "マクロを繰り返しの中で使える");

    printf("--- 読み取り: 誤りの位置と理由 ---\n");
    ExpectError("A c x", 1, 5, "知らない命令です(x)", "知らない命令");
    ExpectError("A\nA c v16", 2, 5, "v の後ろは0〜15", "範囲外の音量(2行目)");
    ExpectError("A c5", 1, 3, "使えない長さ", "使えない長さ");
    ExpectError("A c64.", 1, 3, "付点が細かすぎます", "細かすぎる付点");
    ExpectError("A o8 b >", 1, 8, "上げられません", "オクターブの上限");
    ExpectError("A o0 c < c", 1, 8, "下げられません", "オクターブの下限");
    ExpectError("A n127 k1 n127", 1, 11, "高すぎ", "ノート番号の範囲外(移調で128)");
    ExpectError("A [c", 1, 3, "[ が閉じていません", "閉じていない [");
    ExpectError("A c]", 1, 4, "対応する [ がありません", "対応する [ が無い ]");
    ExpectError("A [c]1", 1, 5, "2〜255", "繰り返し1回");
    ExpectError("A [[[[[c]]]]]", 1, 7, "4段まで", "入れ子5段");
    ExpectError("A c : d", 1, 5, "[ ] の中でしか", "[ ] の外の :");
    ExpectError("A L c L d", 1, 7, "1つまで", "L を2回");
    ExpectError("A [L c]", 1, 4, "[ ] の中には", "[ ] の中の L");
    ExpectError("A c L v5", 0, 0, "L の後ろに音符も休符もありません", "L の後ろが空(無限ループになる)");
    ExpectError("A $nope", 1, 3, "定義されていないマクロ", "未定義のマクロ");
    ExpectError("#macro a = c\n#macro b = $a\nA $b", 3, 3, "別のマクロは呼べません", "マクロからマクロ");
    ExpectError("#macro a = c v99\nA d $a", 2, 5, "マクロ$a: v の後ろは0〜15", "マクロの中の誤りは呼んだ位置で報告");
    ExpectError("#macro a = [c\nA $a", 2, 3, "閉じていません", "マクロの中で閉じていない [");
    ExpectError("#macro a = c\n#macro a = d\nA $a", 2, 8, "2回定義", "マクロの二重定義");
    ExpectError("#tempo 10\nA c", 1, 8, "30〜300", "#tempo の範囲外");
    ExpectError("A t20", 1, 3, "30〜300", "t の範囲外");
    ExpectError("X c", 1, 1, "行の先頭は", "行頭が A〜D でも # でもない");
    ExpectError("A @sine c", 1, 3, "知らない波形", "知らない波形の名前");
    ExpectError("A @9", 1, 3, "0〜7", "範囲外の波形の番号");
    ExpectError("A E9", 1, 3, "-7〜7", "範囲外の減衰");
    ExpectError("A c ド", 1, 5, "使えない文字", "音符の中の日本語");
    {
        std::string longline = "A ";
        for(int i = 0; i < 600; i++) longline += "c";
        ExpectError(longline, 1, 1, "行が長すぎます", "長すぎる行");
    }
    {
        std::string many;
        for(int i = 0; i < 300; i++) many += "A c\n";
        const MmlResult r = Compile(many, 64);
        if(r.ok || !strstr(r.message.c_str(), "曲が長すぎます")) printf("       実際: ok=%d %s\n", r.ok, r.message.c_str());
        check(!r.ok && strstr(r.message.c_str(), "曲が長すぎます"), "置き場に入らない曲");
    }

    printf("--- 読み取り: 警告 ---\n");
    {
        const MmlResult r = Compile("A L c d\nB L c");
        check(r.ok && strstr(r.warning.c_str(), "L から後ろの長さ"), "L から後ろの長さが違えば警告(読めはする)");
    }
    {
        const MmlResult r = Compile("A [c >]2");
        check(r.ok && strstr(r.warning.c_str(), "オクターブ"), "繰り返しの中でオクターブが戻らなければ警告");
    }
    {
        const MmlResult r = Compile("A L c d\nB L e f");
        check(r.ok && r.warning.empty(), "長さが揃っていれば警告なし");
    }

    printf("--- 読み取り: SDのファイル ---\n");
    {
        OSData::SD_usable = true;
        HostSd::files["/music/t.mml"] = "#title ファイル\r\nA c d\r\n";
        MmlFileSource src("/music/t.mml");
        MmlResult r;
        check(src.ok() && compiler.compile(src, buf, sizeof(buf), r) && r.ok, "ファイルから読める(CRLF)");
        check(EventsAre(0, {{60,48},{62,48}}) && strcmp(r.title.c_str(), "ファイル") == 0, "中身と曲名");
        MmlFileSource none("/music/none.mml");
        check(!none.ok(), "無いファイルは開けない");
    }
}

// ================================================================
// 2. シーケンサー
// ================================================================

// 音の出ている区間(開始サンプル, 長さ)を拾う
struct Span { size_t start, len; int16_t peak; };
static std::vector<Span> Spans(const std::vector<int16_t>& v){
    std::vector<Span> out;
    size_t i = 0;
    while(i < v.size()){
        if(v[i] == 0){ i++; continue; }
        const size_t s = i;
        int16_t peak = 0;
        while(i < v.size() && v[i] != 0){ peak = std::max<int16_t>(peak, (int16_t)abs(v[i])); i++; }
        out.push_back({s, i - s, peak});
    }
    return out;
}

// 鳴らして n サンプル分の波形を取る(render()を細かく区切って、区切り方に依らないことも確かめる)
static std::vector<int16_t> Play(const std::string& mml, size_t n, ChipSynth::Engine& e, MusicPlayer& p, size_t step = 64){
    const MmlResult r = Compile(mml);
    if(!r.ok) printf("       読み取りに失敗: %s\n", r.message.c_str());
    p.start(e, buf, r.size);
    std::vector<int16_t> v(n);
    for(size_t i = 0; i < n; i += step) p.render(e, v.data() + i, std::min(step, n - i));
    return v;
}

static void TestPlayer(){
    //テンポ120: 4分音符 = 0.5秒 = 11025サンプル(1ティック = 229.6875サンプル)
    printf("--- シーケンサー: 時間 ---\n");
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        //q4 = 長さの半分だけ鳴らす。音(ラ440Hz)の立ち上がりは波形が正から始まるので区間の頭が分かる
        const auto v = Play("A q4 a a a", kRate * 2, e, p, 37);
        const auto s = Spans(v);
        bool ok = s.size() >= 3;
        //矩形波は符号が変わる瞬間も0にならないので、1音 = 1区間
        for(size_t i = 0; ok && i < 3; i++){
            ok = (s[i].start == i * 11025) && (s[i].len == 5513 || s[i].len == 5512);
        }
        if(!ok) for(auto& x : s) printf("       区間 %zu +%zu\n", x.start, x.len);
        check(ok, "テンポ120の4分音符が11025サンプルごとに鳴り、q4で半分(0.25秒)で切れる");
        check(!p.playing(), "最後まで行ったら止まる");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        const auto v = Play("A q4 a t240 a a", kRate * 2, e, p, 1000);
        const auto s = Spans(v);
        //1音目はテンポ120で11025、2音目以降は240で5512.5サンプルずつ
        check(s.size() == 3 && s[0].start == 0 && s[1].start == 11025 && s[2].start == 11025 + 5513,
              "t で途中からテンポが変わる(端数は持ち越す)");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        const auto v = Play("A q8 a r a", kRate * 2, e, p);
        const auto s = Spans(v);
        check(s.size() == 2 && s[0].len == 11025 && s[1].start == 22050, "q8は切らない。休符で止まる");
    }
    {
        //長く鳴らしてもずれない(1万ティック後の音符の位置)
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        const auto v = Play("#tempo 137\nA r1^1^1^1^1^1^1^1^1^1^1^1 q4 a", kRate * 30, e, p, 509);
        const auto s = Spans(v);
        //192×12 = 2304ティック。1ティック = 22050×60/(137×48) サンプル
        const double expect = 2304.0 * 22050 * 60 / (137.0 * 48);
        check(s.size() == 1 && std::abs((double)s[0].start - expect) <= 1.0, "長い曲でも音の位置がずれない(整数の積み上げ)");
    }

    printf("--- シーケンサー: 繰り返し・ループ ---\n");
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        check(Spans(Play("A q4 l16 [a]3", kRate, e, p)).size() == 3, "[ ]3 で3回");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        //c d c d c = 5音(最後の回は : の後ろを飛ばす)
        check(Spans(Play("A q4 l16 [a : a]3", kRate, e, p)).size() == 5, "[ : ]3 は最後の回だけ : の後ろを飛ばす");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        check(Spans(Play("A q4 l32 [[a]2 a]3", kRate, e, p)).size() == 9, "入れ子の繰り返し(2+1)×3");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        const auto v = Play("A q4 l16 a L a", kRate * 3, e, p);
        //16分 = 2756.25サンプル。3秒で約24音
        const auto s = Spans(v);
        check(p.playing() && s.size() >= 23, "L があれば止めるまで鳴り続ける");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        Play("A q4 l16 L a\nB c1^1^1", kRate, e, p);
        p.stop(e);
        std::vector<int16_t> v(1000);
        p.render(e, v.data(), v.size());
        check(!p.playing() && Spans(v).empty(), "stop()で止まり、音も消える");
    }

    printf("--- シーケンサー: 状態 ---\n");
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        //マクロの中の v5 は抜けたら戻る(外側は v15)
        const auto s = Spans(Play("#macro x = v5 c\nA q4 v15 $x c", kRate * 2, e, p));
        const int32_t full = ChipSynth::Engine::kChannelAmplitude;
        check(s.size() == 2 && s[0].peak == full * 5 / 15 && s[1].peak >= full - 1, "マクロの中で変えた音量は抜けると戻る");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        //2チャンネル同時
        const MmlResult r = Compile("A q4 a\nB q4 r a");
        p.start(e, buf, r.size);
        std::vector<int16_t> v(100);
        p.render(e, v.data(), v.size());
        check(e.activeMask() == 0x1, "A だけ鳴っている");
        std::vector<int16_t> w(11025);
        p.render(e, w.data(), w.size());
        check(e.activeMask() == 0x2, "4分音符の後は B だけ");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        //効果音に借りられたチャンネルへは触らない(曲は進む)
        const MmlResult r = Compile("A q8 l8 a a a a a a a a");
        p.setBorrowed(0x1);
        p.start(e, buf, r.size);
        std::vector<int16_t> v(kRate);
        p.render(e, v.data(), v.size());
        check(Spans(v).empty(), "借りられている間は曲の音を鳴らさない");
        p.setBorrowed(0);
        p.render(e, v.data(), v.size());
        //テンポ120の8分 = 5512.5サンプル。2秒目の頭(=5音目の途中)ではまだ鳴らず、次の音符(6音目)から鳴る
        const auto s = Spans(v);
        check(s.size() == 1 && s[0].start >= 5512 * 1 - 1 && s[0].start <= 5513 * 1 + 1,
              "返されたら次の音符から鳴らす(鳴りかけの音は鳴らし直さない)");
    }
    {
        ChipSynth::Engine e(kRate); MusicPlayer p(kRate);
        check(!p.start(e, buf, 4), "短すぎるデータは鳴らさない");
        uint8_t bad[32] = {'X', 'Y'};
        check(!p.start(e, bad, sizeof(bad)), "形の違うデータは鳴らさない");
    }
}

// ================================================================
// 3. SoundFunctions
// ================================================================
static int ReadHook(int pin){ return pin == AUDIO_DETECT ? LOW : -1; }   //アンプは刺さっている

static void Drain(unsigned long& now, int steps = 3){
    for(int i = 0; i < steps; i++){
        I2S::last->consume(I2S::last->queued.size());
        now += 1;
        SoundFunctions::UpdateAt(now);
        SoundFunctions::Core1StepAt(now);
    }
}

static void TestSoundFunctions(){
    HostGpio::read_hook = &ReadHook;
    unsigned long now = 1000;
    SoundFunctions::SetupAt(now);
    SoundFunctions::Core1StepAt(now);
    check(SoundFunctions::IsAvailable(), "準備: アンプあり");

    printf("--- SoundFunctions: 曲 ---\n");
    MmlResult r;
    check(!SoundFunctions::MusicPlaying(), "最初は鳴っていない");
    check(SoundFunctions::MusicPlayText("#title 一曲目\nA L l8 c d e f", 26, &r), "MusicPlayText");
    check(r.ok && strcmp(SoundFunctions::MusicTitle(), "一曲目") == 0, "曲名");
    check(SoundFunctions::MusicPlaying(), "頼んだ直後から鳴っている扱い(2コア目がまだ受け取っていなくても)");
    Drain(now);
    check(SoundFunctions::MusicPlaying() && (SoundFunctions::ActiveChannels() & 0x1), "2コア目が鳴らしている");

    const char* bad = "A c v99";
    check(!SoundFunctions::MusicPlayText(bad, strlen(bad), &r) && r.line == 1 && r.col == 5, "読めない曲は誤りの位置を返す");
    Drain(now);
    check(SoundFunctions::MusicPlaying() && strcmp(SoundFunctions::MusicTitle(), "一曲目") == 0,
          "読めなかったときは前の曲がそのまま鳴り続ける");

    printf("--- SoundFunctions: 効果音との同居 ---\n");
    {
        ChipSynth::Note n;
        n.freq_x16 = 1000 * 16;
        n.length_ms = 50;
        SoundFunctions::Play(0, n);
        Drain(now);
        check(SoundFunctions::MusicPlaying(), "効果音を鳴らしても曲は止まらない");
        SoundFunctions::StopAll();
        Drain(now);
        check(SoundFunctions::MusicPlaying(), "StopAll(効果音を全部止める)でも曲は止まらない");
        SoundFunctions::Stop(0);
        Drain(now);
        check(SoundFunctions::MusicPlaying(), "曲が使っているチャンネルを Stop しても曲は止まらない");
    }

    printf("--- SoundFunctions: 置き場の入れ替え ---\n");
    {
        const char* a = "A L c";
        const char* b = "A L d";
        check(SoundFunctions::MusicPlayText(a, strlen(a), &r), "2曲目(もう片方の置き場へ)");
        //2コア目が2曲目を受け取る前に3曲目を頼むと、1曲目の置き場はまだ2コア目が読んでいるかもしれない
        check(!SoundFunctions::MusicPlayText(b, strlen(b), &r) && strstr(r.message.c_str(), "片付け"),
              "2コア目が前の曲を手放すまでは、その置き場へ書かない");
        Drain(now);
        check(SoundFunctions::MusicPlayText(b, strlen(b), &r), "手放した後は書ける");
        Drain(now);
        check(SoundFunctions::MusicPlaying(), "差し替えた曲が鳴っている");
    }

    printf("--- SoundFunctions: 止める ---\n");
    SoundFunctions::MusicStop();
    check(!SoundFunctions::MusicPlaying(), "止めた直後から鳴っていない扱い");
    Drain(now);
    check(!SoundFunctions::MusicPlaying() && SoundFunctions::ActiveChannels() == 0, "2コア目も止めた");
    {
        const char* once = "A l16 c d";
        SoundFunctions::MusicPlayText(once, strlen(once), &r);
        for(int i = 0; i < 200; i++) Drain(now, 1);
        check(!SoundFunctions::MusicPlaying(), "L の無い曲は終われば鳴っていない扱い");
    }

    printf("--- SoundFunctions: SDのファイル ---\n");
    {
        OSData::SD_usable = true;
        HostSd::files["/music/demo.mml"] = "A L c d e";
        check(SoundFunctions::MusicPlayFile("/music/demo.mml", &r), "MusicPlayFile");
        check(strcmp(SoundFunctions::MusicTitle(), "demo.mml") == 0, "#title が無ければファイル名");
        check(!SoundFunctions::MusicPlayFile("/music/none.mml", &r) && strstr(r.message.c_str(), "開けません"),
              "無いファイル");
        SoundFunctions::MusicStop();
        Drain(now);
    }
}

int main(){
    TestCompiler();
    TestPlayer();
    TestSoundFunctions();

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
