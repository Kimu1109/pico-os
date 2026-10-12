// USBドライブのセクタの橋渡し(MscSectorBridge)の検証。RAMディスクを相手にする。
// 確かめること:
//   - 64バイトずつの読み出しで、SDを読むのは1セクタ1回
//   - 64バイトずつの書き込みで、頭からなら読まずに溜めて1回だけ書く
//   - 1セクタまるごとの書き込みはそのまま書く / キャッシュ中のセクタなら中身も合わせる
//   - 途中から/隙間をあけた書き込みは、残りをSDから読んで混ぜる
//   - 書きかけのセクタを読むと、書いた分 + SDの残り が見える
//   - 範囲外・SDの失敗は -1
#include "storage/Msc_Sector_Bridge.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static constexpr uint32_t kSectors = 64;
static std::vector<uint8_t> disk(kSectors * 512);
static int disk_reads = 0, disk_writes = 0;
static bool fail_io = false;

static bool DiskRead(uint32_t lba, uint8_t* dst){
    if(fail_io) return false;
    disk_reads++;
    memcpy(dst, &disk[lba * 512], 512);
    return true;
}
static bool DiskWrite(uint32_t lba, const uint8_t* src){
    if(fail_io) return false;
    disk_writes++;
    memcpy(&disk[lba * 512], src, 512);
    return true;
}

static void Fill(uint32_t lba, uint8_t seed){
    for(int i = 0; i < 512; i++) disk[lba * 512 + i] = (uint8_t)(seed + i);
}

int main(){
    static MscSectorBridge b;   // 1KBあるのでスタックに置かない
    for(uint32_t s = 0; s < kSectors; s++) Fill(s, (uint8_t)(s * 7));
    b.reset(&DiskRead, &DiskWrite, kSectors);
    check(b.sectorCount() == kSectors, "セクタ数");

    // ---- 読み ----
    uint8_t got[512];
    disk_reads = 0;
    bool ok = true;
    for(uint32_t off = 0; off < 512; off += 64) ok &= b.read(3, off, got + off, 64) == 64;
    check(ok && memcmp(got, &disk[3 * 512], 512) == 0, "64バイトずつ読むと1セクタぶんの中身");
    check(disk_reads == 1, "SDを読むのは1回");
    b.read(4, 0, got, 64);
    check(disk_reads == 2, "別のセクタへ移ると読む");

    // ---- 頭から64バイトずつ書く ----
    uint8_t data[512];
    for(int i = 0; i < 512; i++) data[i] = (uint8_t)(0xA0 ^ i);
    disk_reads = disk_writes = 0;
    ok = true;
    for(uint32_t off = 0; off < 512; off += 64) ok &= b.write(10, off, data + off, 64) == 64;
    check(ok && memcmp(&disk[10 * 512], data, 512) == 0, "64バイトずつ書くと1セクタぶん書かれる");
    check(disk_reads == 0 && disk_writes == 1, "読まずに、最後に1回だけ書く");
    check(b.sectors_written == 1, "書いたセクタ数を数える");

    // ---- まるごと ----
    disk_writes = 0;
    b.read(11, 0, got, 64);
    for(int i = 0; i < 512; i++) data[i] = (uint8_t)(0x33 + i * 3);
    check(b.write(11, 0, data, 512) == 512 && disk_writes == 1, "まるごとはそのまま書く");
    check(b.read(11, 64, got, 64) == 64 && memcmp(got, data + 64, 64) == 0, "キャッシュ中のセクタも新しい中身になる");

    // ---- 途中から書く ----
    Fill(20, 0x10);
    std::vector<uint8_t> expect(disk.begin() + 20 * 512, disk.begin() + 21 * 512);
    uint8_t part[64];
    memset(part, 0xEE, sizeof(part));
    disk_reads = disk_writes = 0;
    check(b.write(20, 128, part, 64) == 64, "途中から書ける");
    memcpy(&expect[128], part, 64);
    check(b.flush() && disk_writes == 1 && disk_reads == 1, "途中からはSDを読んで混ぜ、flushで書く");
    check(memcmp(&disk[20 * 512], expect.data(), 512) == 0, "書いていない所は元のまま");

    // ---- 頭から書きかけで読む ----
    Fill(21, 0x40);
    expect.assign(disk.begin() + 21 * 512, disk.begin() + 22 * 512);
    memset(part, 0x5A, sizeof(part));
    b.write(21, 0, part, 64);
    b.write(21, 64, part, 64);
    memcpy(&expect[0], part, 64);
    memcpy(&expect[64], part, 64);
    ok = true;
    for(uint32_t off = 0; off < 512; off += 64) ok &= b.read(21, off, got + off, 64) == 64;
    check(ok && memcmp(got, expect.data(), 512) == 0, "書きかけのセクタを読むと書いた分+SDの残り");
    check(b.flush() && memcmp(&disk[21 * 512], expect.data(), 512) == 0, "flushで混ぜた中身が書かれる");

    // ---- 隙間をあけて書く ----
    Fill(22, 0x70);
    expect.assign(disk.begin() + 22 * 512, disk.begin() + 23 * 512);
    memset(part, 0x01, sizeof(part));
    b.write(22, 0, part, 64);
    memcpy(&expect[0], part, 64);
    memset(part, 0x02, sizeof(part));
    b.write(22, 256, part, 64);
    memcpy(&expect[256], part, 64);
    check(b.flush() && memcmp(&disk[22 * 512], expect.data(), 512) == 0, "隙間をあけても残りは元のまま");

    // ---- 別のセクタへ移るときに書く ----
    disk_writes = 0;
    memset(part, 0x77, sizeof(part));
    b.write(30, 0, part, 64);
    check(disk_writes == 0, "書きかけはまだ書かない");
    b.read(31, 0, got, 64);
    check(disk_writes == 1 && disk[30 * 512] == 0x77, "別のセクタを読む前に書きかけを書く");

    // ---- 誤り ----
    check(b.read(kSectors, 0, got, 64) == -1, "範囲外のセクタは-1");
    check(b.read(0, 500, got, 64) == -1, "セクタをはみ出す長さは-1");
    fail_io = true;
    check(b.read(40, 0, got, 64) == -1, "SDの読み出しの失敗は-1");
    check(b.write(41, 0, data, 512) == -1, "SDの書き込みの失敗は-1");
    fail_io = false;
    check(b.read(40, 0, got, 64) == 64 && memcmp(got, &disk[40 * 512], 64) == 0, "失敗の後も読める");

    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
