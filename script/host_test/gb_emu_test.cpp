// Game Boyエミュ(GbEmu / GameBoyPad / GameBoyView)のテスト。
//
// 市販のROMは同梱できないので、テストの中でヘッダとほんの数命令だけのROMを組み立てて使う。
//   - load() の断り方(SD無し/無い/大きすぎ/短すぎ/チェックサム/非対応MBC/セーブ領域が大きすぎ)
//   - カートリッジRAMへの書き込みが .sav に残り、読み込み直すと戻ること
//   - ボタンがゲーム側から読めること(JOYPのA)
//   - 不正な命令で止まっても落ちずに crashed() になること(Peanut-GBの誤り通知から戻らない経路)
//   - 画面の「変わった行」の受け渡しと、GameBoyViewが積むdirty矩形
//   - 操作パッドのタップ位置 → ボタン(十字キーの8方向/A/B/SELECT/START/ROM/戻る、指を滑らせたとき)
//   - 音源チップへの書き込みが時刻付きで GbAudioSink へ渡ること、読み出し(NR52等)の答え方
//   - 内蔵フラッシュへのROMの置き場(GbRomFlash): 書く・同じROMなら書かない・変わったら/壊れたら書き直す・
//     途中でやめると置いていない扱い・大きすぎる・書き込みの失敗、そこからの起動(load(path, image, size))。
//     フラッシュはNORと同じ振る舞い(消すと0xFF、書くとAND)の偽物
// 実際の描画(1.5倍の拡大)は、PCビルドで dmg-acid2 を表示して確認する。
#include "gb/Gb_Emu.hpp"
#include "gb/Gb_Rom_Flash.hpp"
#include "storage/Flash_Area.hpp"
#include "gui/widgets/apps/GameBoyPad.hpp"
#include "gui/widgets/apps/GameBoyView.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ---- モック ----
static Rect last_dirty = {0, 0, 0, 0};
static int dirty_calls = 0;
void PICO_GFX::MarkDirty(const Rect& r){ last_dirty = r; dirty_calls++; }
void LogFunctions::Log(LogType, const char*, ...){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eq_int(long a, long e, const char* label){
    const bool ok = (a == e);
    printf("%s %-50s 実測=%ld 期待=%ld\n", ok ? "[ OK ]" : "[FAIL]", label, a, e);
    if(!ok) failures++;
}

// ---- テスト用のROMを組み立てる ----
// 0x100: NOP; JP 0x0150 → 0x150 から code を置く。ヘッダのチェックサムは正しく計算する
static std::string MakeRom(uint8_t cart_type, uint8_t ram_code, const std::vector<uint8_t>& code,
                           size_t size = 32768, bool break_checksum = false){
    std::string rom(size, '\0');
    const uint8_t entry[] = { 0x00, 0xC3, 0x50, 0x01 };
    memcpy(&rom[0x100], entry, sizeof(entry));
    memcpy(&rom[0x134], "TESTROM", 7);
    rom[0x147] = (char)cart_type;
    rom[0x148] = 0x00; // 32KiB
    rom[0x149] = (char)ram_code;
    uint8_t x = 0;
    for(int i = 0x134; i <= 0x14C; i++) x = (uint8_t)(x - (uint8_t)rom[i] - 1);
    rom[0x14D] = (char)(break_checksum ? (uint8_t)(x + 1) : x);
    for(size_t i = 0; i < code.size(); i++) rom[0x150 + i] = (char)code[i];
    return rom;
}

// 何もしない(JR -2 で回り続ける)
static const std::vector<uint8_t> kIdle = { 0x18, 0xFE };

static void RunFrames(GbEmu& emu, int n){
    for(int i = 0; i < n; i++) emu.runFrame();
}

static void testLoadErrors(){
    GbEmu emu;

    OSData::SD_usable = false;
    check(emu.load("/gb/a.gb") == GbEmu::LoadError::NoSd, "SD無し → NoSd");
    OSData::SD_usable = true;

    check(emu.load("/gb/none.gb") == GbEmu::LoadError::NotFound, "無いファイル → NotFound");
    check(emu.load(nullptr) == GbEmu::LoadError::NotFound, "nullptr → NotFound");

    HostSd::files["/gb/big.gb"] = MakeRom(0x00, 0x00, kIdle, GbEmu::kMaxRomBytes + 1);
    check(emu.load("/gb/big.gb") == GbEmu::LoadError::TooLarge, "256KiB超 → TooLarge");

    HostSd::files["/gb/short.gb"] = std::string(0x100, '\0');
    check(emu.load("/gb/short.gb") == GbEmu::LoadError::TooSmall, "ヘッダが無い → TooSmall");

    HostSd::files["/gb/sum.gb"] = MakeRom(0x00, 0x00, kIdle, 32768, true);
    check(emu.load("/gb/sum.gb") == GbEmu::LoadError::InvalidChecksum, "チェックサム違い → InvalidChecksum");

    HostSd::files["/gb/mbc.gb"] = MakeRom(0x20, 0x00, kIdle); // MBC6
    check(emu.load("/gb/mbc.gb") == GbEmu::LoadError::Unsupported, "対応外のMBC → Unsupported");

    HostSd::files["/gb/ram.gb"] = MakeRom(0x1B, 0x04, kIdle); // MBC5+RAM(128KiB)
    check(emu.load("/gb/ram.gb") == GbEmu::LoadError::SaveTooLarge, "128KiBのセーブ → SaveTooLarge");

    check(!emu.loaded(), "失敗したら何も持たない");

    HostSd::files["/gb/ok.gb"] = MakeRom(0x00, 0x00, kIdle, GbEmu::kMaxRomBytes);
    check(emu.load("/gb/ok.gb") == GbEmu::LoadError::None, "ちょうど256KiBは読める");
    check(emu.loaded(), "読めたらloaded()");
    check(strcmp(emu.title(), "TESTROM") == 0, "ヘッダの題名");
    RunFrames(emu, 3);
    check(!emu.crashed(), "何もしないROMは止まらない");

    HostSd::files["/gb/dir.gb"] = MakeRom(0x00, 0x00, kIdle);
    check(emu.load("/gb/dir.gb") == GbEmu::LoadError::None, "読み込み済みでも別のROMを読める");
    emu.unload();
    check(!emu.loaded(), "unload()");
}

static void testSaveAndJoypad(){
    // MBC1+RAM+BATTERY(8KiB)。RAMを有効にして、ボタンの読み取り結果を0xA000へ書き続ける
    //   LD A,0x0A / LD (0x0000),A           … RAMを有効にする
    // loop:
    //   LD A,0x10 / LDH (0x00),A            … JOYP: ボタン側(A/B/SELECT/START)を選ぶ
    //   LDH A,(0x00) / LD (0xA000),A
    //   JR loop
    const std::vector<uint8_t> code = {
        0x3E, 0x0A, 0xEA, 0x00, 0x00,
        0x3E, 0x10, 0xE0, 0x00,
        0xF0, 0x00, 0xEA, 0x00, 0xA0,
        0x18, 0xF5,
    };
    HostSd::files["/gb/save.gb"] = MakeRom(0x03, 0x02, code);
    HostSd::files.erase("/gb/save.sav");

    {
        GbEmu emu;
        check(emu.load("/gb/save.gb") == GbEmu::LoadError::None, "MBC1+RAMのROMを読める");
        RunFrames(emu, 2);
        check(emu.hasUnsavedData(), "カートリッジRAMへの書き込みで未保存になる");
        emu.setButtons(GbEmu::A);
        RunFrames(emu, 2);
        check(emu.writeSave(), "writeSave()");
        check(!emu.hasUnsavedData(), "書き出したら未保存ではなくなる");
        check(HostSd::files.count("/gb/save.sav") == 1, ".savができる");
        check(HostSd::files.count("/gb/save.sav.part") == 0, "一時ファイルは残らない");
        eq_int((long)HostSd::files["/gb/save.sav"].size(), 8192, ".savの大きさ = セーブ領域");
        eq_int((uint8_t)HostSd::files["/gb/save.sav"][0] & 0x0F, 0x0E, "Aを押している(0 = 押している)");

        emu.setButtons(0);
        RunFrames(emu, 2);
        //unload()(デストラクタ)で自動的に書き出される
    }
    eq_int((uint8_t)HostSd::files["/gb/save.sav"][0] & 0x0F, 0x0F, "離したぶんが終了時に保存される");

    // 読み込み直すとセーブが戻る。書き換わらないように、何もしないROMを同じ名前で置き直す
    HostSd::files["/gb/save.sav"][100] = 0x5A;
    HostSd::files["/gb/save.gb"] = MakeRom(0x03, 0x02, kIdle);
    {
        GbEmu emu;
        check(emu.load("/gb/save.gb") == GbEmu::LoadError::None, "もう一度読める");
        RunFrames(emu, 2);
        check(!emu.hasUnsavedData(), "書き込まないROMは未保存にならない");
        check(!emu.writeSave(), "未保存が無ければ書かない");
    }
    eq_int((uint8_t)HostSd::files["/gb/save.sav"][100], 0x5A, ".savの中身はそのまま");

    // 大きさの違う.savは読まない(別のゲームのものかもしれない)。書き換えもしない
    HostSd::files["/gb/save.sav"] = std::string(100, '\x11');
    {
        GbEmu emu;
        check(emu.load("/gb/save.gb") == GbEmu::LoadError::None, "大きさの違う.savがあっても起動できる");
    }
    eq_int((long)HostSd::files["/gb/save.sav"].size(), 100, "大きさの違う.savは上書きしない");
}

static void testCrash(){
    // 0xD3 はGBに無い命令
    HostSd::files["/gb/bad.gb"] = MakeRom(0x00, 0x00, { 0xD3 });
    GbEmu emu;
    check(emu.load("/gb/bad.gb") == GbEmu::LoadError::None, "不正な命令のROMも読み込みは通る");
    emu.runFrame();
    check(emu.crashed(), "不正な命令で止まる(落ちない)");
    check(strstr(emu.crashMessage(), "0x0150") != nullptr, "止まった番地が分かる");
    emu.runFrame(); // 止まった後は何もしない
    check(emu.crashed(), "止まったまま");
}

// ---- 音 ----
struct FakeSink : public GbAudioSink {
    struct W { uint32_t cycle; uint8_t reg; uint8_t val; };
    int begins = 0, ends = 0, frames = 0;
    uint8_t active = 0;
    std::vector<W> writes;
    // GbAudioLink の決まり: begin/end の区切りはフレームの境目にだけ置ける(書き込みの後は endFrame が先)
    bool open = false;
    int misplaced = 0;
    void begin() override { if(open) misplaced++; begins++; writes.clear(); open = false; }
    void write(uint32_t c, uint8_t r, uint8_t v) override { writes.push_back({c, r, v}); open = true; }
    void endFrame() override { frames++; open = false; }
    void end() override { if(open) misplaced++; ends++; open = false; }
    uint8_t activeChannels() override { return active; }
};

static void testSound(){
    //   LD A,0x80 / LDH (0x26),A    … NR52 電源
    //   LD A,0xF0 / LDH (0x17),A    … NR22 音量15
    //   LD A,0x80 / LDH (0x16),A    … NR21 デューティ50%
    //   LD A,0xD6 / LDH (0x18),A    … NR23 (f=1750の下位)
    //   LD A,0x86 / LDH (0x19),A    … NR24 トリガー + 上位
    // loop: INC B / LD A,B / LDH (0x24),A / JR loop   … NR50へ書き続ける(時刻の進み方を見る)
    const std::vector<uint8_t> code = {
        0x3E, 0x80, 0xE0, 0x26,
        0x3E, 0xF0, 0xE0, 0x17,
        0x3E, 0x80, 0xE0, 0x16,
        0x3E, 0xD6, 0xE0, 0x18,
        0x3E, 0x86, 0xE0, 0x19,
        0x04, 0x78, 0xE0, 0x24, 0x18, 0xFA,
    };
    HostSd::files["/gb/snd.gb"] = MakeRom(0x00, 0x00, code);

    FakeSink sink;
    GbEmu emu;
    emu.setAudioSink(&sink);
    check(emu.load("/gb/snd.gb") == GbEmu::LoadError::None, "音を鳴らすROMを読める");
    eq_int(sink.begins, 1, "読み込むと音を始める");
    check(!sink.writes.empty() && sink.writes[0].reg == 0x16, "起動時の値(NR52)が渡る");

    sink.writes.clear();
    emu.runFrame();
    eq_int(sink.frames, 1, "1フレームごとに区切る");
    bool trig = false;
    for(auto& w : sink.writes) if(w.reg == 0x09 && w.val == 0x86) trig = true;
    check(trig, "NR24(トリガー)が渡る");
    bool mono = true;
    uint32_t max_cycle = 0, n50 = 0;
    for(size_t i = 1; i < sink.writes.size(); i++) if(sink.writes[i].cycle < sink.writes[i - 1].cycle) mono = false;
    for(auto& w : sink.writes){ if(w.cycle > max_cycle) max_cycle = w.cycle; if(w.reg == 0x14) n50++; }
    check(mono, "時刻はフレームの中で戻らない");
    check(max_cycle < 70224 && max_cycle > 60000, "時刻はフレームの頭から終わりまで(0〜70223)");
    check(n50 > 1000, "ループの書き込みが全部渡る");

    // 読み出し: 書き込み専用のビットは1で返る
    eq_int(emu.audioRead(0xFF16), 0xBF, "NR21はデューティだけ読める");
    eq_int(emu.audioRead(0xFF18), 0xFF, "NR23は読めない(0xFF)");
    eq_int(emu.audioRead(0xFF17), 0xF0, "NR22は読める");
    sink.active = 0x02;
    eq_int(emu.audioRead(0xFF26), 0xF2, "NR52: 電源 + 音源が知らせた鳴っているチャンネル");
    sink.active = 0;
    eq_int(emu.audioRead(0xFF26), 0xF0, "NR52: 鳴っていなければ下位は0");
    emu.audioWrite(0xFF19, 0x86);
    eq_int(emu.audioRead(0xFF26), 0xF2, "NR52: このフレームにトリガーしたチャンネルはすぐ1になる");
    emu.audioWrite(0xFF26, 0x00);
    eq_int(emu.audioRead(0xFF26), 0x70, "電源を切るとNR52は0x70");
    eq_int(emu.audioRead(0xFF17), 0x00, "電源を切るとNR22が消える");
    emu.audioWrite(0xFF17, 0xF0);
    eq_int(emu.audioRead(0xFF17), 0x00, "電源が切れている間は書けない");
    emu.audioWrite(0xFF30, 0x12);
    eq_int(emu.audioRead(0xFF30), 0x12, "波形メモリは電源が切れていても書ける");

    const int ends_before = sink.ends;
    emu.unload();
    eq_int(sink.ends - ends_before, 1, "閉じると音を止める");

    // 止まったエミュの音も止める
    HostSd::files["/gb/bad2.gb"] = MakeRom(0x00, 0x00, { 0x3E, 0x80, 0xE0, 0x26, 0xD3 });
    check(emu.load("/gb/bad2.gb") == GbEmu::LoadError::None, "止まるROMを読める");
    const int ends2 = sink.ends;
    emu.runFrame();
    check(emu.crashed(), "不正な命令で止まる");
    eq_int(sink.ends - ends2, 1, "止まったら音も止める");
    emu.unload();
    eq_int(sink.ends - ends2, 1, "二重に止めない");

    // 渡し先が無くても控えだけで動く
    GbEmu quiet;
    check(quiet.load("/gb/snd.gb") == GbEmu::LoadError::None, "渡し先無しでも読める");
    quiet.runFrame();
    eq_int(quiet.audioRead(0xFF17), 0xF0, "渡し先無しでも読み出しは答える");
}

static void testScreen(){
    HostSd::files["/gb/idle.gb"] = MakeRom(0x00, 0x00, kIdle);
    GbEmu emu;

    GameBoyView view(0, STATUSBAR_HEIGHT, &emu);
    view.onFrame();
    check(!emu.row(0), "読み込み前は画面が無い");

    check(emu.load("/gb/idle.gb") == GbEmu::LoadError::None, "読める");
    check(emu.row(0) != nullptr && emu.row(GbEmu::kHeight - 1) != nullptr, "行が取れる");
    check(emu.row(GbEmu::kHeight) == nullptr, "範囲外の行はnullptr");

    dirty_calls = 0;
    view.onFrame();
    eq_int(dirty_calls, 1, "読み込み直後は全体を描く");
    eq_int(last_dirty.y, STATUSBAR_HEIGHT, "dirtyの上端");
    eq_int(last_dirty.h, GameBoyView::kViewH, "dirtyの高さ = 216");

    int first = 0, last = 0;
    check(!emu.takeChangedRows(first, last), "渡した後は変化なし");

    // 真っ白のまま(VRAMが空)なので、何フレーム進めても変わった行は無い
    RunFrames(emu, 3);
    dirty_calls = 0;
    view.onFrame();
    eq_int(dirty_calls, 0, "画面が変わらなければdirtyを積まない");
}

static void testPad(){
    const int top = STATUSBAR_HEIGHT + GameBoyView::kViewH;
    GameBoyPad pad(0, top, SCREEN_WIDTH);

    int rom_calls = 0, back_calls = 0;
    pad.setOnMenu([&](){ rom_calls++; });
    pad.setOnBack([&](){ back_calls++; });

    auto touch = [&](int x, int y){ OSData::touchX = x; OSData::touchY = top + y; };
    auto press = [&](int x, int y){ touch(x, y); pad.causeOnPressStart(); return pad.getPressed(); };

    // 十字キーの中心は (43, 42)
    eq_int(press(10, 42), GbEmu::Left, "十字キーの左");
    pad.causeOnPressEnd();
    eq_int(pad.getPressed(), 0, "離すと何も押していない");
    eq_int(press(76, 42), GbEmu::Right, "十字キーの右");
    pad.causeOnPressEnd();
    eq_int(press(43, 8), GbEmu::Up, "十字キーの上");
    pad.causeOnPressEnd();
    eq_int(press(43, 78), GbEmu::Down, "十字キーの下");
    pad.causeOnPressEnd();
    eq_int(press(68, 17), GbEmu::Up | GbEmu::Right, "斜め(右上)は2方向");
    pad.causeOnPressEnd();
    eq_int(press(44, 43), 0, "十字キーの真ん中は何も押さない");
    pad.causeOnPressEnd();

    eq_int(press(216, 30), GbEmu::A, "A");
    // 指を滑らせるとBへ移る
    touch(178, 54);
    pad.causeOnPressMove();
    eq_int(pad.getPressed(), GbEmu::B, "AからBへ滑らせる");
    touch(20, 42);
    pad.causeOnPressMove();
    eq_int(pad.getPressed(), GbEmu::Left, "Bから十字キーへ滑らせる");
    touch(100, 70);
    pad.causeOnPressMove();
    eq_int(pad.getPressed(), 0, "ROMの上へ滑らせても押さない");
    pad.causeOnPressEnd();
    eq_int(rom_calls, 0, "ゲームのボタンから滑ってきてもROMは反応しない");

    eq_int(press(120, 14), GbEmu::Select, "SELECT");
    pad.causeOnPressEnd();
    eq_int(press(120, 40), GbEmu::Start, "START");
    pad.causeOnPressEnd();

    eq_int(press(100, 68), 0, "ROMはゲームのボタンではない");
    pad.causeOnPressEnd();
    eq_int(rom_calls, 1, "ROMの上で離すと呼ばれる");

    press(140, 68);
    touch(200, 40);
    pad.causeOnPressMove();
    pad.causeOnPressEnd();
    eq_int(back_calls, 0, "戻るから指を外して離すと呼ばれない");
    press(140, 68);
    pad.causeOnPressEnd();
    eq_int(back_calls, 1, "戻るの上で離すと呼ばれる");

    pad.renderForce(); // スタブのframeへ描いて落ちないこと
}

// 途中保存(ステートセーブ)。カートリッジRAMの数を数え続けるROMで、保存した時点へ戻ることを見る
static void testState(){
    //   LD A,0x0A / LD (0x0000),A   … RAMを有効にする
    //   LD HL,0xA000
    // loop:
    //   INC (HL) / JR loop
    const std::vector<uint8_t> code = {
        0x3E, 0x0A, 0xEA, 0x00, 0x00,
        0x21, 0x00, 0xA0,
        0x34, 0x18, 0xFD,
    };
    HostSd::files["/gb/st.gb"] = MakeRom(0x03, 0x02, code);
    HostSd::files.erase("/gb/st.sav");
    HostSd::files.erase("/gb/states/st.st0");
    HostSd::files.erase("/gb/states/st.st1");

    GbEmu emu;
    check(emu.saveState(GbEmu::kManualSlot) == GbEmu::StateError::NotLoaded, "読み込む前は保存できない");
    check(emu.load("/gb/st.gb") == GbEmu::LoadError::None, "数えるROMを読める");
    check(!emu.hasState(GbEmu::kManualSlot), "最初は状態が無い");
    check(emu.loadState(GbEmu::kManualSlot) == GbEmu::StateError::NotFound, "無い状態は読めない");

    FixedString<PICO_PATH_LEN> path;
    check(emu.statePath(GbEmu::kManualSlot, path) && strcmp(path.c_str(), "/gb/states/st.st1") == 0, "手動の枠は .st1");
    check(emu.statePath(GbEmu::kAutoSlot, path) && strcmp(path.c_str(), "/gb/states/st.st0") == 0, "自動の枠は .st0");

    RunFrames(emu, 3);
    check(emu.saveState(GbEmu::kManualSlot) == GbEmu::StateError::None, "途中保存できる");
    check(emu.hasState(GbEmu::kManualSlot), "保存すると状態がある");
    check(HostSd::files.count("/gb/states/st.st1.part") == 0, "一時ファイルは残らない");
    emu.writeSave();
    const uint8_t at_save = (uint8_t)HostSd::files["/gb/st.sav"][0];

    RunFrames(emu, 5);
    emu.writeSave();
    const uint8_t later = (uint8_t)HostSd::files["/gb/st.sav"][0];
    check(later != at_save, "進めると数が変わる");

    check(emu.loadState(GbEmu::kManualSlot) == GbEmu::StateError::None, "途中から再開できる");
    check(emu.hasUnsavedData(), "読み戻したセーブ領域は未保存扱い");
    emu.writeSave();
    eq_int((uint8_t)HostSd::files["/gb/st.sav"][0], at_save, "保存した時点の数へ戻る");
    int first = -1, last = -1;
    check(emu.takeChangedRows(first, last) && first == 0 && last == GbEmu::kHeight - 1, "読み戻すと全体を描き直す");
    RunFrames(emu, 1);
    check(!emu.crashed(), "読み戻した後も動く");
    emu.writeSave();
    check((uint8_t)HostSd::files["/gb/st.sav"][0] != at_save, "読み戻した続きから数え続ける");

    // 壊れたファイル: 中身を1バイト変えるとチェックサムで断り、今の状態は変わらない
    emu.writeSave();
    const uint8_t before_bad = (uint8_t)HostSd::files["/gb/st.sav"][0];
    std::string good = HostSd::files["/gb/states/st.st1"];
    std::string bad = good;
    bad[bad.size() / 2] ^= 0x55;
    HostSd::files["/gb/states/st.st1"] = bad;
    check(emu.loadState(GbEmu::kManualSlot) == GbEmu::StateError::BadFormat, "壊れた状態は断る");
    check(!emu.crashed(), "断っても止まらない");
    emu.writeSave();
    eq_int((uint8_t)HostSd::files["/gb/st.sav"][0], before_bad, "断ったときは今の状態のまま");
    HostSd::files["/gb/states/st.st1"] = good.substr(0, good.size() - 10);
    check(emu.loadState(GbEmu::kManualSlot) == GbEmu::StateError::BadFormat, "短いファイルは断る");
    HostSd::files["/gb/states/st.st1"] = good;

    // 別のROMの状態は読まない(同じ大きさのセーブ領域でも)
    std::string other = MakeRom(0x03, 0x02, kIdle);
    memcpy(&other[0x134], "OTHERROM", 8);
    uint8_t x = 0;
    for(int i = 0x134; i <= 0x14C; i++) x = (uint8_t)(x - (uint8_t)other[i] - 1);
    other[0x14D] = (char)x;
    HostSd::files["/gb/other.gb"] = other;
    HostSd::files["/gb/states/other.st1"] = good;
    HostSd::files.erase("/gb/other.sav");
    {
        GbEmu emu2;
        check(emu2.load("/gb/other.gb") == GbEmu::LoadError::None, "別のROMを読める");
        check(emu2.loadState(GbEmu::kManualSlot) == GbEmu::StateError::OtherRom, "別のROMの状態は断る");
    }

    // 書き込みが落ち着いたら.savを書き出す
    emu.unload();
    HostSd::files.erase("/gb/st.sav");
    check(emu.load("/gb/st.gb") == GbEmu::LoadError::None, "数えるROMを読み直す");
    RunFrames(emu, 1);                       // カートリッジRAMへ書く
    check(!emu.flushSaveIfIdle(1000), "書いた直後は書き出さない");
    check(!emu.flushSaveIfIdle(1000 + GbEmu::kSaveIdleMs - 1), "落ち着くまでは書き出さない");
    check(emu.flushSaveIfIdle(1000 + GbEmu::kSaveIdleMs), "落ち着いたら書き出す");
    check(HostSd::files.count("/gb/st.sav") == 1, ".savができる");
    RunFrames(emu, 1);
    check(!emu.flushSaveIfIdle(1000 + GbEmu::kSaveIdleMs * 2), "また書いた直後は書き出さない");
    check(!emu.flushSaveIfIdle(1000 + GbEmu::kSaveIdleMs * 3), "前の書き出しから間が無ければ書き出さない");
    check(emu.flushSaveIfIdle(1000 + GbEmu::kSaveIdleMs + GbEmu::kSaveMinIntervalMs), "間が空けば書き出す");

    // 音: 読み込んだ直後(フレームを1つも走らせていない=起動時の書き込みが区切られていない)に
    // 状態を読み戻しても、閉じても、区切りをフレームの途中に置かない(置くと2コア目が回り続けた)
    {
        FakeSink sink;
        GbEmu e2;
        e2.setAudioSink(&sink);
        check(e2.load("/gb/st.gb") == GbEmu::LoadError::None, "音付きで読める");
        check(sink.open, "起動時の書き込みはまだ区切られていない");
        check(e2.loadState(GbEmu::kManualSlot) == GbEmu::StateError::None, "フレーム前に読み戻せる");
        eq_int(sink.misplaced, 0, "読み戻しで区切りをフレームの途中に置かない");
        check(sink.begins == 2 && sink.ends == 1, "音源を作り直す(end→begin)");
        RunFrames(e2, 2);
        e2.unload();
        e2.setAudioSink(&sink);
        check(e2.load("/gb/st.gb") == GbEmu::LoadError::None, "もう一度読める");
        e2.unload();
        eq_int(sink.misplaced, 0, "フレーム前に閉じても区切りをフレームの途中に置かない");
    }
}

// ---- 内蔵フラッシュの偽物 ----
class FakeFlash : public FlashArea {
    public:
        explicit FakeFlash(uint32_t sz) : mem(sz, 0xFF) {}
        uint32_t size() const override { return (uint32_t)mem.size(); }
        uint32_t baseAddress() const override { return 0x1024F000u; } // 実機と同じく先頭は64KBに揃っていない
        const uint8_t* data() const override { return mem.data(); }
        bool erase(uint32_t off, uint32_t len) override {
            if(off % kSectorBytes || len % kSectorBytes || off + len > mem.size()) { bad++; return false; }
            if(len == kBlockBytes){
                block_erases++;
                if((baseAddress() + off) % kBlockBytes) bad++; // 揃っていない64KB
            }else if(len == kSectorBytes){
                sector_erases++;
            }else{
                bad++;
            }
            memset(&mem[off], 0xFF, len);
            return true;
        }
        bool program(uint32_t off, const uint8_t* p, uint32_t len) override {
            if(fail_program) return false;
            if(off % kPageBytes || len % kPageBytes || off + len > mem.size()) { bad++; return false; }
            for(uint32_t i = 0; i < len; i++){
                if(mem[off + i] != 0xFF) not_erased++;
                mem[off + i] &= p[i];
            }
            programs++;
            return true;
        }
        std::vector<uint8_t> mem;
        int block_erases = 0, sector_erases = 0, programs = 0, not_erased = 0, bad = 0;
        bool fail_program = false;
        void resetCounts(){ block_erases = sector_erases = programs = not_erased = bad = 0; }
};

static void RunFlash(GbRomFlash& job){
    for(int i = 0; i < 100000 && job.state() == GbRomFlash::State::Writing; i++) job.step(0);
}

static void testRomFlash(){
    OSData::SD_usable = true;
    FakeFlash flash(1728u * 1024u);
    eq_int(GbRomFlash::Capacity(&flash), 1724L * 1024, "置けるのは領域-4KB");
    eq_int(GbRomFlash::Capacity(nullptr), 0, "領域が無ければ0");

    // 1MiBのMBC1。各バンクの頭にバンク番号を書いておく
    std::string rom = MakeRom(0x01, 0x00, kIdle, 1024u * 1024u);
    rom[0x148] = 0x05;
    {
        uint8_t x = 0;
        for(int i = 0x134; i <= 0x14C; i++) x = (uint8_t)(x - (uint8_t)rom[i] - 1);
        rom[0x14D] = (char)x;
    }
    for(uint32_t b = 1; b < 64; b++) rom[b * 0x4000] = (char)b;
    rom[rom.size() - 1] = 0x5A;
    HostSd::files["/gb/big1m.gb"] = rom;

    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/big1m.gb") == GbRomFlash::State::Writing, "初めては書く");
        check(!flash.holdsRawData(), "書いている間は見出しが無い");
        job.step(0);
        check(job.state() == GbRomFlash::State::Writing && job.written() < job.total(), "1回のstep()では終わらない");
        RunFlash(job);
        check(job.state() == GbRomFlash::State::Done, "書き終わる");
        check(!job.reused(), "書いた");
        eq_int(job.imageSize(), (long)rom.size(), "大きさ");
        check(memcmp(job.image(), rom.data(), rom.size()) == 0, "中身がSDと同じ");
        check(job.image() == flash.data() + GbRomFlash::kDataOffset, "フラッシュをそのまま指す");
        check(flash.holdsRawData(), "書き終えたら生のデータの印");
        eq_int(flash.not_erased, 0, "消してある所へだけ書く");
        eq_int(flash.bad, 0, "消す/書くの揃え方");
        eq_int(flash.block_erases, 16, "1MiBは64KBの消去16回");

        // そこから起動する
        GbEmu emu;
        check(emu.load("/gb/big1m.gb", job.image(), job.imageSize()) == GbEmu::LoadError::None, "フラッシュのROMで起動");
        check(emu.romIsExternal(), "外の置き場を使っている");
        eq_int(emu.romSize(), (long)rom.size(), "エミュのROMの大きさ");
        eq_int(emu.readRom(63u * 0x4000), 63, "最後のバンクを読める");
        eq_int(emu.readRom((uint32_t)rom.size() - 1), 0x5A, "末尾を読める");
        RunFrames(emu, 3);
        check(!emu.crashed(), "動く");
        emu.unload(); // フラッシュを free しない(ASanで分かる)
        check(!emu.loaded(), "unload()");
    }

    // 2回目は書かない
    flash.resetCounts();
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/big1m.gb") == GbRomFlash::State::Done, "同じROMは書かずに Done");
        check(job.reused(), "使い回した");
        eq_int(flash.block_erases + flash.sector_erases + flash.programs, 0, "消しも書きもしない");
    }

    // 末尾が変わったら書き直す
    rom[rom.size() - 2] = 0x33;
    HostSd::files["/gb/big1m.gb"] = rom;
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/big1m.gb") == GbRomFlash::State::Writing, "末尾が変わったら書き直す");
        RunFlash(job);
        check(job.state() == GbRomFlash::State::Done && memcmp(job.image(), rom.data(), rom.size()) == 0, "書き直した中身");
    }

    // フラッシュの中身が化けていたら書き直す
    flash.mem[GbRomFlash::kDataOffset + 500000] ^= 0x01;
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/big1m.gb") == GbRomFlash::State::Writing, "フラッシュが化けていたら書き直す");
        RunFlash(job);
        check(job.state() == GbRomFlash::State::Done && memcmp(job.image(), rom.data(), rom.size()) == 0, "直った");
    }

    // 途中でやめる → 置いていない扱い → 次は書き直す
    {
        GbRomFlash job(&flash);
        HostSd::files["/gb/small.gb"] = MakeRom(0x00, 0x00, kIdle, 32768);
        check(job.begin("/gb/small.gb") == GbRomFlash::State::Writing, "別のROM");
        job.step(0);
        job.cancel();
        check(!flash.holdsRawData(), "途中でやめたら置いていない");
    }
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/small.gb") == GbRomFlash::State::Writing, "やめた後は書き直す");
        RunFlash(job);
        check(job.state() == GbRomFlash::State::Done, "小さなROMも書ける");
        eq_int(flash.bad, 0, "端の消去は4KB");
    }

    // 1MiBを超え、領域に入らない
    HostSd::files["/gb/huge.gb"] = std::string(2u * 1024u * 1024u, '\0');
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/huge.gb") == GbRomFlash::State::Failed, "2MiBは置けない");
        check(strstr(job.message(), "1724KB") != nullptr, "置ける大きさを知らせる");
    }
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/none.gb") == GbRomFlash::State::Failed, "無いROM");
        GbRomFlash none(nullptr);
        check(none.begin("/gb/small.gb") == GbRomFlash::State::Failed, "領域が無い");
    }

    // 書き込みに失敗する
    {
        flash.fail_program = true;
        GbRomFlash job(&flash);
        check(job.begin("/gb/big1m.gb") == GbRomFlash::State::Writing, "書き始める");
        RunFlash(job);
        check(job.state() == GbRomFlash::State::Failed, "書けなければ Failed");
        check(!flash.holdsRawData(), "失敗したら置いていない");
        flash.fail_program = false;
    }

    // SDが途中で使えなくなる
    {
        GbRomFlash job(&flash);
        check(job.begin("/gb/big1m.gb") == GbRomFlash::State::Writing, "書き始める(2)");
        job.step(0);
        OSData::SD_usable = false;
        job.step(0);
        check(job.state() == GbRomFlash::State::Failed, "SDが無くなったら Failed");
        OSData::SD_usable = true;
    }

    // 無効にする
    {
        GbRomFlash job(&flash);
        job.begin("/gb/small.gb");
        RunFlash(job);
        check(flash.holdsRawData(), "置いた");
        GbRomFlash::Invalidate(&flash);
        check(!flash.holdsRawData(), "Invalidate()で置いていない");
    }

    GbEmu emu;
    check(emu.load("/gb/x.gb", nullptr, 100) == GbEmu::LoadError::NotFound, "imageがnullptr");
    static const uint8_t tiny[16] = {};
    check(emu.load("/gb/x.gb", tiny, sizeof(tiny)) == GbEmu::LoadError::TooSmall, "短いimage");
}

int main(){
    testLoadErrors();
    testState();
    testSaveAndJoypad();
    testCrash();
    testSound();
    testScreen();
    testPad();
    testRomFlash();

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
