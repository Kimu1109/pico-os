// Game Boyエミュ(GbEmu / GameBoyPad / GameBoyView)のテスト。
//
// 市販のROMは同梱できないので、テストの中でヘッダとほんの数命令だけのROMを組み立てて使う。
//   - load() の断り方(SD無し/無い/大きすぎ/短すぎ/チェックサム/非対応MBC/セーブ領域が大きすぎ)
//   - カートリッジRAMへの書き込みが .sav に残り、読み込み直すと戻ること
//   - ボタンがゲーム側から読めること(JOYPのA)
//   - 不正な命令で止まっても落ちずに crashed() になること(Peanut-GBの誤り通知から戻らない経路)
//   - 画面の「変わった行」の受け渡しと、GameBoyViewが積むdirty矩形
//   - 操作パッドのタップ位置 → ボタン(十字キーの8方向/A/B/SELECT/START/ROM/戻る、指を滑らせたとき)
// 実際の描画(1.5倍の拡大)は、PCビルドで dmg-acid2 を表示して確認する。
#include "gb/Gb_Emu.hpp"
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
    pad.setOnRom([&](){ rom_calls++; });
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

int main(){
    testLoadErrors();
    testSaveAndJoypad();
    testCrash();
    testScreen();
    testPad();

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
