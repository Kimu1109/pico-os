#pragma once
#include <cstdint>
#include <cstring>

// USBマスストレージ(UsbDriveFunctions)の「USBの小分けの読み書き ⇔ SDの512バイトのセクタ」の橋渡し。
//
// TinyUSB は READ10/WRITE10 を1回あたり CFG_TUD_MSC_EP_BUFSIZE(arduino-picoでは64バイト)ずつに
// 分けてコールバックしてくる(offset はセクタの中の位置)。素直にSDへ渡すと1セクタを8回読み、
// 書き込みは「読む→64バイト差し替え→書く」を8回することになるので、直近の1セクタを持つ:
//   - 読み: 同じセクタの続きはメモリから写す(SDを読むのは1セクタ1回)
//   - 書き: セクタの頭から書き始めたら読まずに溜め、最後まで溜まったら1回だけ書く。
//           途中から書き始めた(普通は来ない)ときだけ先にSDから読んで混ぜる
//   - 別のセクタへ移る前・flush() で、溜めたままの分を書く
// SDへの読み書きは関数ポインタで受け取るので、描画にもUSBにも依存しない(ホストテストはRAMディスク)。
// RAMは1KB(セクタ2つぶん)。
class MscSectorBridge {
    public:
        constexpr static uint32_t kSectorSize = 512;
        using ReadFn  = bool (*)(uint32_t lba, uint8_t* dst);
        using WriteFn = bool (*)(uint32_t lba, const uint8_t* src);

        void reset(ReadFn r, WriteFn w, uint32_t sector_count){
            read_fn = r;
            write_fn = w;
            sectors = sector_count;
            cache_lba = kNone;
            valid = false;
            dirty = false;
            filled = 0;
            sectors_read = 0;
            sectors_written = 0;
            errors = 0;
        }

        uint32_t sectorCount() const { return sectors; }

        // 読んだバイト数を返す(誤りは-1)
        int32_t read(uint32_t lba, uint32_t offset, void* buf, uint32_t n){
            if(!inRange(lba, offset, n)) return fail();
            if(cache_lba != lba){
                if(!flush()) return fail();
                if(!read_fn(lba, cache)){ cache_lba = kNone; return fail(); }
                cache_lba = lba;
                valid = true;
                filled = 0;
                sectors_read++;
            }else if(!makeValid()){
                return fail();
            }
            memcpy(buf, cache + offset, n);
            return (int32_t)n;
        }

        // 書いた(受け取った)バイト数を返す(誤りは-1)
        int32_t write(uint32_t lba, uint32_t offset, const uint8_t* buf, uint32_t n){
            if(!inRange(lba, offset, n)) return fail();

            if(offset == 0 && n == kSectorSize){
                //1セクタまるごと。溜めずにそのまま書く
                if(cache_lba != lba && !flush()) return fail();
                if(!write_fn(lba, buf)) return fail();
                sectors_written++;
                if(cache_lba == lba){ memcpy(cache, buf, kSectorSize); valid = true; dirty = false; }
                return (int32_t)n;
            }

            if(cache_lba != lba){
                if(!flush()) return fail();
                cache_lba = lba;
                dirty = false;
                if(offset == 0){
                    valid = false;      //頭から書く。中身は書いた分で埋まっていく
                    filled = 0;
                }else{
                    if(!read_fn(lba, cache)){ cache_lba = kNone; return fail(); }
                    sectors_read++;
                    valid = true;
                }
            }
            if(!valid && offset > filled && !makeValid()) return fail();   //隙間ができる書き方

            memcpy(cache + offset, buf, n);
            dirty = true;
            if(!valid && offset <= filled && offset + n > filled) filled = offset + n;
            if(!valid && filled >= kSectorSize){ valid = true; filled = 0; }

            if(offset + n >= kSectorSize){
                if(!flush()) return fail();
            }
            return (int32_t)n;
        }

        // 溜めたままの分を書く
        bool flush(){
            if(!dirty) return true;
            if(!makeValid()) return false;
            if(!write_fn(cache_lba, cache)){ errors++; return false; }
            sectors_written++;
            dirty = false;
            return true;
        }

        // 数(画面の表示用。USBのコールバックの中で増えるので volatile)
        volatile uint32_t sectors_read = 0;
        volatile uint32_t sectors_written = 0;
        volatile uint32_t errors = 0;

    private:
        constexpr static uint32_t kNone = 0xFFFFFFFFu;

        ReadFn read_fn = nullptr;
        WriteFn write_fn = nullptr;
        uint32_t sectors = 0;

        uint8_t cache[kSectorSize];
        uint8_t scratch[kSectorSize];
        uint32_t cache_lba = kNone;
        bool valid = false;     // cache の全体がそのセクタの中身(書いた分を含む)
        bool dirty = false;     // SDへ書いていない分がある
        uint32_t filled = 0;    // valid=false の間、頭から書いた長さ

        bool inRange(uint32_t lba, uint32_t offset, uint32_t n) const {
            return read_fn && write_fn && lba < sectors && offset < kSectorSize && n <= kSectorSize - offset;
        }
        int32_t fail(){ errors++; return -1; }

        // 頭から一部だけ書いたセクタ: 書いていない残りをSDから読んで埋める
        bool makeValid(){
            if(valid) return true;
            if(cache_lba == kNone) return false;
            if(!read_fn(cache_lba, scratch)) return false;
            sectors_read++;
            memcpy(cache + filled, scratch + filled, kSectorSize - filled);
            valid = true;
            filled = 0;
            return true;
        }
};
