#include "gb/Gb_Emu.hpp"

#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// 音はまだ出せない(SUMMARY.md #11)ので、APUは持たない。そのぶん軽い
#define ENABLE_SOUND 0
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
        case LoadError::TooLarge:        return "ROMが大きすぎます(今は256KBまで)";
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

    this->impl = static_cast<Impl*>(malloc(sizeof(Impl)));
    if(!this->impl){
        LOG_APP_FAIL("GbEmu: エミュ本体(%uB)を確保できません", (unsigned)sizeof(Impl));
        this->unload();
        return LoadError::OutOfMemory;
    }
    memset(this->impl, 0, sizeof(Impl));

    const enum gb_init_error_e init_err = gb_init(&this->impl->gb,
        &CbRomRead, &CbCartRamRead, &CbCartRamWrite, &CbError, this);
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
    this->save_path.assign(rom_path);
    const char* slash = strrchr(rom_path, '/');
    const char* dot = strrchr(rom_path, '.');
    if(dot && (!slash || dot > slash)){
        this->save_path.assign(rom_path, (size_t)(dot - rom_path));
    }
    if(!this->save_path.append(".sav")){
        this->save_path.clear(); //パスが長すぎる。別のファイルを指さないよう保存しない
    }
    if(this->cart_ram_size > 0) this->readSaveFile();

    char title_buf[17];
    gb_get_rom_name(&this->impl->gb, title_buf);
    this->title_.assign(title_buf);

    memset(this->impl->fb, 0, sizeof(this->impl->fb));
    gb_init_lcd(&this->impl->gb, &CbDrawLine);
    this->changed_first = 0;
    this->changed_last = kHeight - 1;

    LOG_APP_OK("GbEmu: %s を起動しました(題名\"%s\" ROM %uB / セーブ %uB)",
        rom_path, this->title_.c_str(), (unsigned)this->rom_size, (unsigned)this->cart_ram_size);
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

    this->rom = static_cast<uint8_t*>(malloc(file_size));
    if(!this->rom){
        LOG_APP_FAIL("GbEmu: ROM(%uB)を確保できません", (unsigned)file_size);
        f.close();
        return LoadError::OutOfMemory;
    }

    size_t done = 0;
    while(done < file_size){
        const size_t want = (file_size - done < 4096) ? (file_size - done) : 4096;
        const int got = f.read(this->rom + done, want);
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

void GbEmu::unload(){
    if(this->cart_ram_dirty) this->writeSave();

    free(this->impl);
    this->impl = nullptr;
    free(this->rom);
    this->rom = nullptr;
    this->rom_size = 0;
    free(this->cart_ram);
    this->cart_ram = nullptr;
    this->cart_ram_size = 0;
    this->cart_ram_dirty = false;

    this->crashed_ = false;
    this->crash_message.clear();
    this->title_.clear();
    this->save_path.clear();
    this->changed_first = -1;
    this->changed_last = -1;
}

void GbEmu::runFrame(){
    if(!this->impl || this->crashed_) return;

    // CbError()→onError()からここへ跳んで戻る。Peanut-GBの状態は途中のままなので、
    // 以降は二度と進めない(crashed_で止める)
    if(setjmp(this->impl->on_error) != 0) return;
    gb_run_frame(&this->impl->gb);
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
