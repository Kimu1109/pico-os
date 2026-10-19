#include "gb/Gb_Emu.hpp"

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "storage/SD_IO.hpp"

#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 音源チップ(0xFF10〜0xFF3F)の読み書きは audio_read()/audio_write() で受け取る(GbEmu::audioRead/audioWrite)。
// 音そのものは2コア目の GbApu が作る(GbAudioSink経由)
#define ENABLE_SOUND 1
static uint8_t audio_read(const uint16_t addr);
static void audio_write(const uint16_t addr, const uint8_t val);
#define ENABLE_LCD 1
// DMGの4段階だけで描く(物体/背景のパレットの区別は使わない)
#define PEANUT_GB_12_COLOUR 0
// peanut_gb.hの実装を取り込むのはこのファイルだけ(README-pico-os.md参照)
#include "peanut_gb.h"

struct GbEmu::Impl {
    struct gb_s gb;
    uint8_t fb[GbEmu::kHeight][GbEmu::kBytesPerRow];
    // Peanut-GBは回復できない誤り(不正な命令)の通知から戻ることを想定していない
    // (戻ると__builtin_unreachable()へ落ちる)ので、runFrame()の入口へ跳んで戻る
    jmp_buf on_error;
};

// audio_read/audio_write には gb_s が渡されないので、今動かしているエミュを覚えておく
// (load()の中のgb_init()と、runFrame()の間だけ)
static GbEmu* g_audio_emu = nullptr;

static uint8_t audio_read(const uint16_t addr){
    return g_audio_emu ? g_audio_emu->audioRead(addr) : 0xFF;
}
static void audio_write(const uint16_t addr, const uint8_t val){
    if(g_audio_emu) g_audio_emu->audioWrite(addr, val);
}

static GbEmu* SelfOf(struct gb_s* gb){
    return static_cast<GbEmu*>(gb->direct.priv);
}

static uint8_t CbRomRead(struct gb_s* gb, const uint_fast32_t addr){
    return SelfOf(gb)->readRom((uint32_t)addr);
}
static uint8_t CbCartRamRead(struct gb_s* gb, const uint_fast32_t addr){
    return SelfOf(gb)->readCartRam((uint32_t)addr);
}
static void CbCartRamWrite(struct gb_s* gb, const uint_fast32_t addr, const uint8_t val){
    SelfOf(gb)->writeCartRam((uint32_t)addr, val);
}
static void CbError(struct gb_s* gb, const enum gb_error_e err, const uint16_t addr){
    SelfOf(gb)->onError((int)err, addr);
}
static void CbDrawLine(struct gb_s* gb, const uint8_t* pixels, const uint_fast8_t line){
    SelfOf(gb)->drawLine(pixels, (int)line);
}

// ヘッダを読むのに要る長さ(0x100〜0x14F)
static constexpr uint32_t kRomHeaderEnd = 0x150;

const char* GbEmu::loadErrorToStr(LoadError e){
    switch(e){
        case LoadError::None:            return "成功";
        case LoadError::NoSd:            return "SDカードが使えません";
        case LoadError::NotFound:        return "ROMを開けません";
        case LoadError::TooLarge:        return "ROMが大きすぎます(RAMに載るのは256KBまで)";
        case LoadError::TooSmall:        return "ROMとして短すぎます";
        case LoadError::OutOfMemory:     return "メモリが足りません";
        case LoadError::ReadFailed:      return "ROMの読み込みに失敗しました";
        case LoadError::InvalidChecksum: return "ROMのヘッダが壊れています";
        case LoadError::Unsupported:     return "対応していないカートリッジです";
        case LoadError::SaveTooLarge:    return "セーブ領域が大きすぎます(32KBまで)";
    }
    return "不明な誤り";
}

GbEmu::LoadError GbEmu::load(const char* rom_path){
    this->unload();
    if(!rom_path || !rom_path[0]) return LoadError::NotFound;

    const LoadError read_err = this->readRomFile(rom_path);
    if(read_err != LoadError::None){
        this->unload();
        return read_err;
    }
    return this->start(rom_path);
}

GbEmu::LoadError GbEmu::load(const char* rom_path, const uint8_t* image, uint32_t size){
    this->unload();
    if(!rom_path || !rom_path[0] || !image) return LoadError::NotFound;
    if(size < kRomHeaderEnd) return LoadError::TooSmall;
    this->rom = image;
    this->rom_size = size;
    this->rom_owned = false;
    return this->start(rom_path);
}

GbEmu::LoadError GbEmu::start(const char* rom_path){
    this->impl = static_cast<Impl*>(malloc(sizeof(Impl)));
    if(!this->impl){
        LOG_APP_FAIL("GbEmu: エミュ本体(%uB)を確保できません", (unsigned)sizeof(Impl));
        this->unload();
        return LoadError::OutOfMemory;
    }
    memset(this->impl, 0, sizeof(Impl));

    //gb_init()の中でNR52へ書くので、音はその前に始める
    memset(this->apu_regs, 0, sizeof(this->apu_regs));
    this->apu_triggered = 0;
    this->apu_last_cycle = 0;
    if(this->audio_sink){
        this->audio_sink->begin();
        this->audio_started = true;
        this->audio_frame_open = false;
    }
    g_audio_emu = this;
    const enum gb_init_error_e init_err = gb_init(&this->impl->gb,
        &CbRomRead, &CbCartRamRead, &CbCartRamWrite, &CbError, this);
    g_audio_emu = nullptr;
    if(init_err != GB_INIT_NO_ERROR){
        this->unload();
        return (init_err == GB_INIT_INVALID_CHECKSUM)
            ? LoadError::InvalidChecksum : LoadError::Unsupported;
    }

    size_t save_size = 0;
    if(gb_get_save_size_s(&this->impl->gb, &save_size) != 0){
        this->unload();
        return LoadError::Unsupported;
    }
    if(save_size > kMaxCartRamBytes){
        this->unload();
        return LoadError::SaveTooLarge;
    }
    if(save_size > 0){
        this->cart_ram = static_cast<uint8_t*>(malloc(save_size));
        if(!this->cart_ram){
            this->unload();
            return LoadError::OutOfMemory;
        }
        //電池の切れたカートリッジと同じく0xFF埋めで始める
        memset(this->cart_ram, 0xFF, save_size);
        this->cart_ram_size = (uint32_t)save_size;
    }

    // "<ROM>.gb" → "<ROM>.sav"(拡張子が無ければ末尾へ足す)
    this->base_path.assign(rom_path);
    const char* slash = strrchr(rom_path, '/');
    const char* dot = strrchr(rom_path, '.');
    if(dot && (!slash || dot > slash)){
        this->base_path.assign(rom_path, (size_t)(dot - rom_path));
    }
    this->save_path.assign(this->base_path.c_str());
    if(!this->save_path.append(".sav")){
        this->save_path.clear(); //パスが長すぎる。別のファイルを指さないよう保存しない
        this->base_path.clear();
    }
    this->rom_checksum = (uint16_t)((this->rom[0x14E] << 8) | this->rom[0x14F]);
    this->cart_ram_touched = false;
    this->cart_ram_last_write_ms = 0;
    this->cart_ram_last_flush_ms = 0;
    if(this->cart_ram_size > 0) this->readSaveFile();

    char title_buf[17];
    gb_get_rom_name(&this->impl->gb, title_buf);
    this->title_.assign(title_buf);

    //起動ROM(ここでは飛ばしている)が終わった直後の値にしておく。左右の音量(NR50)と振り分け(NR51)を
    //自分で書かないゲームがあるため。トリガーはしないので音は出ない
    this->audioWrite(0xFF11, 0x80);
    this->audioWrite(0xFF12, 0xF3);
    this->audioWrite(0xFF24, 0x77);
    this->audioWrite(0xFF25, 0xF3);

    memset(this->impl->fb, 0, sizeof(this->impl->fb));
    gb_init_lcd(&this->impl->gb, &CbDrawLine);
    this->changed_first = 0;
    this->changed_last = kHeight - 1;

    LOG_APP_OK("GbEmu: %s を起動しました(題名\"%s\" ROM %uB(%s) / セーブ %uB)",
        rom_path, this->title_.c_str(), (unsigned)this->rom_size,
        this->rom_owned ? "RAM" : "内蔵フラッシュ", (unsigned)this->cart_ram_size);
    return LoadError::None;
}

GbEmu::LoadError GbEmu::readRomFile(const char* rom_path){
    if(!OSData::SD_usable) return LoadError::NoSd;

    FsFile f = OSData::SD.open(rom_path);
    if(!f) return LoadError::NotFound;
    if(f.isDirectory()){
        f.close();
        return LoadError::NotFound;
    }

    const size_t file_size = f.fileSize();
    if(file_size > kMaxRomBytes){
        f.close();
        return LoadError::TooLarge;
    }
    if(file_size < kRomHeaderEnd){
        f.close();
        return LoadError::TooSmall;
    }

    uint8_t* buf = static_cast<uint8_t*>(malloc(file_size));
    if(!buf){
        LOG_APP_FAIL("GbEmu: ROM(%uB)を確保できません", (unsigned)file_size);
        f.close();
        return LoadError::OutOfMemory;
    }
    this->rom = buf;
    this->rom_owned = true;

    size_t done = 0;
    while(done < file_size){
        const size_t want = (file_size - done < 4096) ? (file_size - done) : 4096;
        const int got = f.read(buf + done, want);
        if(got <= 0) break;
        done += (size_t)got;
    }
    f.close();
    if(done != file_size) return LoadError::ReadFailed;

    this->rom_size = (uint32_t)file_size;
    return LoadError::None;
}

void GbEmu::readSaveFile(){
    if(this->save_path.empty() || !OSData::SD.exists(this->save_path.c_str())) return;

    FsFile f = OSData::SD.open(this->save_path.c_str());
    if(!f) return;
    const size_t file_size = f.fileSize();
    if(file_size != this->cart_ram_size){
        //大きさの違うセーブは別のゲームのものかもしれない。読まずに白紙で始める
        LOG_APP_WARN("GbEmu: %s の大きさ(%uB)がセーブ領域(%uB)と違うので読みません",
            this->save_path.c_str(), (unsigned)file_size, (unsigned)this->cart_ram_size);
        f.close();
        return;
    }
    size_t done = 0;
    while(done < file_size){
        const int got = f.read(this->cart_ram + done, file_size - done);
        if(got <= 0) break;
        done += (size_t)got;
    }
    f.close();
    if(done != file_size){
        LOG_APP_WARN("GbEmu: %s を読み切れませんでした", this->save_path.c_str());
        memset(this->cart_ram, 0xFF, this->cart_ram_size);
    }
}

bool GbEmu::writeSave(){
    if(!this->cart_ram_dirty || !this->cart_ram || this->save_path.empty()) return false;
    if(!OSData::SD_usable) return false;

    // 一時ファイルへ書いてから差し替える(書いている途中で電源が落ちても前のセーブが残る)
    FixedString<PICO_PATH_LEN> tmp_path(this->save_path.c_str());
    if(!tmp_path.append(".part")) return false;

    FsFile f = OSData::SD.open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){
        LOG_APP_FAIL("GbEmu: %s を書き込み用に開けません", tmp_path.c_str());
        return false;
    }
    const size_t wrote = f.write(this->cart_ram, this->cart_ram_size);
    f.close();
    if(wrote != this->cart_ram_size){
        LOG_APP_FAIL("GbEmu: セーブの書き込みに失敗しました");
        OSData::SD.remove(tmp_path.c_str());
        return false;
    }
    if(OSData::SD.exists(this->save_path.c_str()) && !OSData::SD.remove(this->save_path.c_str())){
        LOG_APP_FAIL("GbEmu: 古いセーブを消せません");
        OSData::SD.remove(tmp_path.c_str());
        return false;
    }
    if(!OSData::SD.rename(tmp_path.c_str(), this->save_path.c_str())){
        LOG_APP_FAIL("GbEmu: セーブを差し替えられません");
        return false;
    }

    this->cart_ram_dirty = false;
    LOG_APP_OK("GbEmu: %s へ保存しました", this->save_path.c_str());
    return true;
}

void GbEmu::audioStop(){
    if(this->audio_started && this->audio_sink){
        if(this->audio_frame_open) this->audio_sink->endFrame();
        this->audio_sink->end();
    }
    this->audio_started = false;
    this->audio_frame_open = false;
}

void GbEmu::unload(){
    if(this->cart_ram_dirty) this->writeSave();
    this->audioStop();
    if(g_audio_emu == this) g_audio_emu = nullptr;

    free(this->impl);
    this->impl = nullptr;
    if(this->rom_owned) free(const_cast<uint8_t*>(this->rom));
    this->rom = nullptr;
    this->rom_owned = false;
    this->rom_size = 0;
    free(this->cart_ram);
    this->cart_ram = nullptr;
    this->cart_ram_size = 0;
    this->cart_ram_dirty = false;

    this->crashed_ = false;
    this->crash_message.clear();
    this->title_.clear();
    this->save_path.clear();
    this->base_path.clear();
    this->rom_checksum = 0;
    this->cart_ram_touched = false;
    this->changed_first = -1;
    this->changed_last = -1;
}

void GbEmu::runFrame(){
    if(!this->impl || this->crashed_) return;

    // CbError()→onError()からここへ跳んで戻る。Peanut-GBの状態は途中のままなので、
    // 以降は二度と進めない(crashed_で止める)
    if(setjmp(this->impl->on_error) != 0){
        g_audio_emu = nullptr;
        this->audioStop();      //止まったエミュの最後の音を鳴らし続けない
        return;
    }
    g_audio_emu = this;
    gb_run_frame(&this->impl->gb);
    g_audio_emu = nullptr;

    if(this->audio_started && this->audio_sink) this->audio_sink->endFrame();
    this->audio_frame_open = false;
    this->apu_triggered = 0;
    this->apu_last_cycle = 0;
}

uint32_t GbEmu::frameCycle() const {
    //Peanut-GBの1フレームはLYが144(VBlankの始まり)になったところで区切られる
    const struct gb_s& gb = this->impl->gb;
    uint32_t c;
    if(gb.hram_io[IO_LCDC] & LCDC_ENABLE){
        const uint32_t line = (uint32_t)(gb.hram_io[IO_LY] + LCD_VERT_LINES - LCD_HEIGHT) % LCD_VERT_LINES;
        c = line * LCD_LINE_CYCLES + (uint32_t)gb.counter.lcd_count;
    }else{
        c = (uint32_t)gb.counter.lcd_off_count;
    }
    if(c >= LCD_FRAME_CYCLES) c = LCD_FRAME_CYCLES - 1;
    return c;
}

uint8_t GbEmu::audioRead(uint16_t addr) const {
    //読むと常に1が返るビット(未使用のビット・書き込み専用のレジスタ)
    static const uint8_t kOr[0x30] = {
        0x80, 0x3F, 0x00, 0xFF, 0xBF,
        0xFF, 0x3F, 0x00, 0xFF, 0xBF,
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
        0xFF, 0xFF, 0x00, 0x00, 0xBF,
        0x00, 0x00, 0x70,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    if(addr < 0xFF10 || addr > 0xFF3F) return 0xFF;
    const uint8_t off = (uint8_t)(addr - 0xFF10);
    if(off == 0x16){
        if(!(this->apu_regs[0x16] & 0x80)) return 0x70;
        const uint8_t active = this->audio_sink ? this->audio_sink->activeChannels() : 0;
        return (uint8_t)(0xF0 | ((active | this->apu_triggered) & 0x0F));
    }
    return (uint8_t)(this->apu_regs[off] | kOr[off]);
}

void GbEmu::audioWrite(uint16_t addr, uint8_t val){
    if(addr < 0xFF10 || addr > 0xFF3F) return;
    const uint8_t off = (uint8_t)(addr - 0xFF10);
    const bool power = (this->apu_regs[0x16] & 0x80) != 0;

    if(off == 0x16){
        if(!(val & 0x80)){
            memset(this->apu_regs, 0, 0x16);    //電源を切るとNR10〜NR51が消える
            this->apu_triggered = 0;
        }
        this->apu_regs[0x16] = val & 0x80;
    }else if(off < 0x16){
        if(!power) return;                      //電源が切れている間は受け付けない
        this->apu_regs[off] = val;
        //トリガー(NRx4のbit7)。DACが入っていれば鳴り始める
        if(val & 0x80){
            switch(off){
                case 0x04: if(this->apu_regs[0x02] & 0xF8) this->apu_triggered |= 0x01; break;
                case 0x09: if(this->apu_regs[0x07] & 0xF8) this->apu_triggered |= 0x02; break;
                case 0x0E: if(this->apu_regs[0x0A] & 0x80) this->apu_triggered |= 0x04; break;
                case 0x13: if(this->apu_regs[0x11] & 0xF8) this->apu_triggered |= 0x08; break;
                default: break;
            }
        }
    }else{
        this->apu_regs[off] = val;              //未使用の番地と波形メモリ
    }

    if(this->audio_started && this->audio_sink){
        uint32_t cycle = this->impl ? this->frameCycle() : 0;
        if(cycle < this->apu_last_cycle) cycle = this->apu_last_cycle;   //LCDの入り切りで戻らないように
        this->apu_last_cycle = cycle;
        this->audio_sink->write(cycle, off, val);
        this->audio_frame_open = true;
    }
}

void GbEmu::setButtons(uint8_t pressed){
    if(!this->impl) return;
    // Peanut-GBは「0 = 押している」
    this->impl->gb.direct.joypad = (uint8_t)~pressed;
}

const uint8_t* GbEmu::row(int y) const {
    if(!this->impl || y < 0 || y >= kHeight) return nullptr;
    return this->impl->fb[y];
}

bool GbEmu::takeChangedRows(int& first, int& last){
    if(this->changed_first < 0) return false;
    first = this->changed_first;
    last = this->changed_last;
    this->changed_first = -1;
    this->changed_last = -1;
    return true;
}

uint8_t GbEmu::readRom(uint32_t addr) const {
    // ヘッダの申告よりファイルが短い(吸い出しが途中まで等)場合に配列の外を読まない
    return (addr < this->rom_size) ? this->rom[addr] : 0xFF;
}

uint8_t GbEmu::readCartRam(uint32_t addr) const {
    return (addr < this->cart_ram_size) ? this->cart_ram[addr] : 0xFF;
}

void GbEmu::writeCartRam(uint32_t addr, uint8_t val){
    if(addr >= this->cart_ram_size) return;
    if(this->cart_ram[addr] == val) return;
    this->cart_ram[addr] = val;
    this->cart_ram_dirty = true;
    this->cart_ram_touched = true;
}

void GbEmu::drawLine(const uint8_t* pixels, int line){
    if(line < 0 || line >= kHeight) return;

    uint8_t packed[kBytesPerRow];
    for(int i = 0; i < kBytesPerRow; i++){
        const uint8_t* p = pixels + i * 4;
        packed[i] = (uint8_t)(((p[0] & 3) << 6) | ((p[1] & 3) << 4) | ((p[2] & 3) << 2) | (p[3] & 3));
    }

    uint8_t* dst = this->impl->fb[line];
    if(memcmp(dst, packed, kBytesPerRow) == 0) return;
    memcpy(dst, packed, kBytesPerRow);

    if(this->changed_first < 0 || line < this->changed_first) this->changed_first = line;
    if(line > this->changed_last) this->changed_last = line;
}

void GbEmu::onError(int error, uint16_t addr){
    static const char* const kNames[] = { "不明な誤り", "不正な命令", "不正な読み出し", "不正な書き込み" };
    const char* name = (error >= 0 && error < 4) ? kNames[error] : kNames[0];

    this->crashed_ = true;
    this->crash_message.clear();
    this->crash_message.appendFormat("%s (0x%04X)", name, (unsigned)addr);
    LOG_APP_FAIL("GbEmu: エミュが停止しました: %s", this->crash_message.c_str());

    longjmp(this->impl->on_error, 1);
}

// ================= 途中保存(ステートセーブ) =================

namespace {
    // ファイルの見出し(ネイティブのバイト順。同じビルドの間でだけ読み書きする)
    struct StateHeader {
        char     magic[4];          // "PGBS"
        uint16_t version;
        uint8_t  ptr_size;          // sizeof(void*)
        uint8_t  reserved;
        uint32_t gb_size;           // sizeof(gb_s)
        uint32_t wram_offset;       // offsetof(gb_s, wram)(配置の違いを見分ける)
        uint32_t fb_size;
        uint32_t rom_size;
        uint16_t rom_checksum;
        uint16_t apu_size;
        uint32_t cart_ram_size;
        char     title[16];
    };
    constexpr uint16_t kStateVersion = 1;
    constexpr uint32_t kFnvInit = 2166136261u;

    uint32_t Fnv(uint32_t h, const uint8_t* p, size_t n){
        for(size_t i = 0; i < n; i++){ h ^= p[i]; h *= 16777619u; }
        return h;
    }

    bool WriteAll(FsFile& f, const void* p, size_t n, uint32_t& h){
        h = Fnv(h, static_cast<const uint8_t*>(p), n);
        return f.write(static_cast<const uint8_t*>(p), n) == n;
    }

    bool ReadAll(FsFile& f, void* p, size_t n){
        size_t done = 0;
        uint8_t* d = static_cast<uint8_t*>(p);
        while(done < n){
            const int got = f.read(d + done, n - done);
            if(got <= 0) return false;
            done += (size_t)got;
        }
        return true;
    }
}

const char* GbEmu::stateErrorToStr(StateError e){
    switch(e){
        case StateError::None:        return "成功";
        case StateError::NotLoaded:   return "ゲームが動いていません";
        case StateError::NoSd:        return "SDカードが使えません";
        case StateError::NoPath:      return "保存先のパスが長すぎます";
        case StateError::NotFound:    return "保存した状態がありません";
        case StateError::WriteFailed: return "状態の書き込みに失敗しました";
        case StateError::ReadFailed:  return "状態の読み込みに失敗しました";
        case StateError::BadFormat:   return "状態のファイルが壊れているか、別の版のものです";
        case StateError::OtherRom:    return "別のROMの状態です";
    }
    return "不明な誤り";
}

bool GbEmu::statePath(int slot, FixedString<PICO_PATH_LEN>& out) const {
    if(!this->impl || this->base_path.empty() || slot < 0 || slot >= kStateSlots) return false;
    out.assign(PICO_Path::DIR::GB_STATES);
    if(!out.append(PICO_IO::filename(this->base_path.c_str()))) return false;
    char ext[8];
    snprintf(ext, sizeof(ext), ".st%d", slot);
    return out.append(ext);
}

bool GbEmu::hasState(int slot) const {
    if(!OSData::SD_usable) return false;
    FixedString<PICO_PATH_LEN> path;
    if(!this->statePath(slot, path)) return false;
    return OSData::SD.exists(path.c_str());
}

GbEmu::StateError GbEmu::saveState(int slot){
    if(!this->impl || this->crashed_) return StateError::NotLoaded;
    if(!OSData::SD_usable) return StateError::NoSd;
    FixedString<PICO_PATH_LEN> path;
    if(!this->statePath(slot, path)) return StateError::NoPath;
    FixedString<PICO_PATH_LEN> tmp(path.c_str());
    if(!tmp.append(".part")) return StateError::NoPath;

    StateHeader h{};
    memcpy(h.magic, "PGBS", 4);
    h.version = kStateVersion;
    h.ptr_size = (uint8_t)sizeof(void*);
    h.gb_size = (uint32_t)sizeof(struct gb_s);
    h.wram_offset = (uint32_t)offsetof(struct gb_s, wram);
    h.fb_size = (uint32_t)sizeof(this->impl->fb);
    h.rom_size = this->rom_size;
    h.rom_checksum = this->rom_checksum;
    h.apu_size = (uint16_t)sizeof(this->apu_regs);
    h.cart_ram_size = this->cart_ram_size;
    strncpy(h.title, this->title_.c_str(), sizeof(h.title));

    if(!OSData::SD.exists(PICO_Path::DIR::GB_STATES)) OSData::SD.mkdir(PICO_Path::DIR::GB_STATES);
    FsFile f = OSData::SD.open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){
        LOG_APP_FAIL("GbEmu: %s を書き込み用に開けません", tmp.c_str());
        return StateError::WriteFailed;
    }
    uint32_t sum = kFnvInit;
    bool ok = WriteAll(f, &h, sizeof(h), sum)
           && WriteAll(f, &this->impl->gb, sizeof(struct gb_s), sum)
           && WriteAll(f, this->impl->fb, sizeof(this->impl->fb), sum)
           && WriteAll(f, this->apu_regs, sizeof(this->apu_regs), sum)
           && (this->cart_ram_size == 0 || WriteAll(f, this->cart_ram, this->cart_ram_size, sum));
    if(ok){
        uint32_t dummy = 0;
        ok = WriteAll(f, &sum, sizeof(sum), dummy);
    }
    f.close();
    if(!ok){
        LOG_APP_FAIL("GbEmu: 状態の書き込みに失敗しました");
        OSData::SD.remove(tmp.c_str());
        return StateError::WriteFailed;
    }
    if(OSData::SD.exists(path.c_str()) && !OSData::SD.remove(path.c_str())){
        OSData::SD.remove(tmp.c_str());
        return StateError::WriteFailed;
    }
    if(!OSData::SD.rename(tmp.c_str(), path.c_str())) return StateError::WriteFailed;

    LOG_APP_OK("GbEmu: 状態を %s へ保存しました", path.c_str());
    return StateError::None;
}

GbEmu::StateError GbEmu::loadState(int slot){
    if(!this->impl) return StateError::NotLoaded;
    if(!OSData::SD_usable) return StateError::NoSd;
    FixedString<PICO_PATH_LEN> path;
    if(!this->statePath(slot, path)) return StateError::NoPath;
    if(!OSData::SD.exists(path.c_str())) return StateError::NotFound;

    const size_t payload = sizeof(struct gb_s) + sizeof(this->impl->fb) + sizeof(this->apu_regs) + this->cart_ram_size;

    // ---- 1周目: 見出しと全体のチェックサムを確かめる(今の状態には触らない) ----
    FsFile f = OSData::SD.open(path.c_str());
    if(!f) return StateError::ReadFailed;
    StateHeader h{};
    if(!ReadAll(f, &h, sizeof(h))){ f.close(); return StateError::BadFormat; }
    if(memcmp(h.magic, "PGBS", 4) != 0 || h.version != kStateVersion
        || h.ptr_size != sizeof(void*) || h.gb_size != sizeof(struct gb_s)
        || h.wram_offset != offsetof(struct gb_s, wram)
        || h.fb_size != sizeof(this->impl->fb) || h.apu_size != sizeof(this->apu_regs)){
        f.close();
        return StateError::BadFormat;
    }
    if(h.rom_size != this->rom_size || h.rom_checksum != this->rom_checksum
        || h.cart_ram_size != this->cart_ram_size
        || strncmp(h.title, this->title_.c_str(), sizeof(h.title)) != 0){
        f.close();
        return StateError::OtherRom;
    }
    if(f.fileSize() != sizeof(h) + payload + sizeof(uint32_t)){ f.close(); return StateError::BadFormat; }

    uint32_t sum = Fnv(kFnvInit, reinterpret_cast<const uint8_t*>(&h), sizeof(h));
    {
        uint8_t buf[512];
        size_t left = payload;
        while(left > 0){
            const size_t want = left < sizeof(buf) ? left : sizeof(buf);
            if(!ReadAll(f, buf, want)){ f.close(); return StateError::ReadFailed; }
            sum = Fnv(sum, buf, want);
            left -= want;
        }
        uint32_t stored = 0;
        if(!ReadAll(f, &stored, sizeof(stored))){ f.close(); return StateError::ReadFailed; }
        if(stored != sum){ f.close(); return StateError::BadFormat; }
    }

    // ---- 2周目: 今の状態へ写す ----
    if(!f.seekSet(sizeof(h))){ f.close(); return StateError::ReadFailed; }
    struct gb_s& gb = this->impl->gb;
    // 写すと消えるもの(関数ポインタ・priv)を控える
    const struct gb_s keep = gb;
    bool ok = ReadAll(f, &gb, sizeof(gb))
           && ReadAll(f, this->impl->fb, sizeof(this->impl->fb))
           && ReadAll(f, this->apu_regs, sizeof(this->apu_regs))
           && (this->cart_ram_size == 0 || ReadAll(f, this->cart_ram, this->cart_ram_size));
    f.close();

    gb.gb_rom_read = keep.gb_rom_read;
    gb.gb_cart_ram_read = keep.gb_cart_ram_read;
    gb.gb_cart_ram_write = keep.gb_cart_ram_write;
    gb.gb_error = keep.gb_error;
    gb.gb_serial_tx = keep.gb_serial_tx;
    gb.gb_serial_rx = keep.gb_serial_rx;
    gb.gb_bootrom_read = keep.gb_bootrom_read;
    gb.display.lcd_draw_line = keep.display.lcd_draw_line;
    gb.direct.priv = keep.direct.priv;
    gb.direct.joypad = keep.direct.joypad;

    if(!ok){
        //検査は通ったのに読めなかった(SDが抜けた等)。途中まで写した状態は信用できないので止める
        this->crashed_ = true;
        this->crash_message.assign("状態の読み込みの途中で失敗しました");
        this->audioStop();
        return StateError::ReadFailed;
    }

    if(this->cart_ram_size > 0){
        this->cart_ram_dirty = true;    //読み戻したセーブ領域も、閉じるときに .sav へ書く
        this->cart_ram_touched = true;
    }
    this->apu_triggered = 0;
    this->apu_last_cycle = 0;
    this->resyncAudio();
    this->changed_first = 0;
    this->changed_last = kHeight - 1;

    LOG_APP_OK("GbEmu: 状態を %s から読み込みました", path.c_str());
    return StateError::None;
}

void GbEmu::resyncAudio(){
    if(!this->audio_sink) return;
    // 音源を作り直し、控えの値を書き直す。NRx4のトリガー(bit7)は落とす(鳴っていた音は、
    // ゲームが次にトリガーするまで止まる。音の途中の位置までは写さないため)
    this->audioStop();
    this->audio_sink->begin();
    this->audio_started = true;
    this->audio_sink->write(0, 0x16, this->apu_regs[0x16]);
    this->audio_frame_open = true;
    if(!(this->apu_regs[0x16] & 0x80)) return;
    for(uint8_t off = 0; off < 0x30; off++){
        if(off == 0x16) continue;
        if(off >= 0x17 && off < 0x20) continue;    //未使用
        uint8_t v = this->apu_regs[off];
        if(off == 0x04 || off == 0x09 || off == 0x0E || off == 0x13) v &= 0x7F;
        this->audio_sink->write(0, off, v);
    }
}

bool GbEmu::flushSaveIfIdle(uint32_t now_ms){
    if(this->cart_ram_touched){
        this->cart_ram_touched = false;
        this->cart_ram_last_write_ms = now_ms;
        return false;
    }
    if(!this->cart_ram_dirty || this->crashed_) return false;
    if(now_ms - this->cart_ram_last_write_ms < kSaveIdleMs) return false;
    if(this->cart_ram_last_flush_ms != 0 && now_ms - this->cart_ram_last_flush_ms < kSaveMinIntervalMs) return false;
    this->cart_ram_last_flush_ms = now_ms == 0 ? 1 : now_ms;
    return this->writeSave();
}
