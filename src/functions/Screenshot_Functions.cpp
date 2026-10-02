#include "functions/Screenshot_Functions.hpp"
#include "functions/GFX_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

namespace {
    constexpr int kPaletteColors = 16;
    constexpr uint32_t kHeaderBytes = 14 + 40 + kPaletteColors * 4;

    void put16(uint8_t* p, uint16_t v){ p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
    void put32(uint8_t* p, uint32_t v){ put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }

    void fail(char* out, size_t size, const char* msg){
        if(out && size > 0) snprintf(out, size, "%s", msg);
    }
}

bool ScreenshotFunctions::Capture(char* out_path, size_t out_size){
    if(!OSData::SD_usable){ fail(out_path, out_size, "SDカードがありません"); return false; }
    if(OSData::frame == nullptr){ fail(out_path, out_size, "画面がありません"); return false; }

    if(!OSData::SD.exists(kDir)) OSData::SD.mkdir(kDir);

    //空いている連番を探す(撮るたびに先頭から数え直さないよう、前回の続きから)
    static int next_no = 1;
    char path[40];
    for(;;){
        snprintf(path, sizeof(path), "%s/shot_%04d.bmp", kDir, next_no);
        if(!OSData::SD.exists(path)) break;
        if(++next_no > 9999){ fail(out_path, out_size, "空き番号がありません"); return false; }
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){ fail(out_path, out_size, "ファイルを作れません"); return false; }

    const int w = SCREEN_WIDTH;
    const int h = SCREEN_HEIGHT;
    const uint32_t row_bytes = ((w * 4 + 31) / 32) * 4;   //BMPの1行は4バイト境界
    const uint32_t image_bytes = row_bytes * h;

    uint8_t header[kHeaderBytes] = {0};
    header[0] = 'B'; header[1] = 'M';
    put32(header + 2, kHeaderBytes + image_bytes);
    put32(header + 10, kHeaderBytes);
    put32(header + 14, 40);                 //BITMAPINFOHEADER
    put32(header + 18, (uint32_t)w);
    put32(header + 22, (uint32_t)h);        //正の値 = 下の行から並ぶ
    put16(header + 26, 1);
    put16(header + 28, 4);
    put32(header + 34, image_bytes);
    put32(header + 46, kPaletteColors);
    //パレットはRGB565(PICO_GFX::COLORS)をBGRAへ
    for(int i = 0; i < kPaletteColors; i++){
        const uint32_t c = (uint32_t)PICO_GFX::COLORS[i];
        const uint8_t r5 = (c >> 11) & 0x1F, g6 = (c >> 5) & 0x3F, b5 = c & 0x1F;
        uint8_t* e = header + 54 + i * 4;
        e[0] = (b5 << 3) | (b5 >> 2);
        e[1] = (g6 << 2) | (g6 >> 4);
        e[2] = (r5 << 3) | (r5 >> 2);
    }

    bool ok = (f.write(header, kHeaderBytes) == kHeaderBytes);

    uint8_t row[((SCREEN_WIDTH * 4 + 31) / 32) * 4];
    for(int y = h - 1; y >= 0 && ok; y--){
        memset(row, 0, sizeof(row));
        for(int x = 0; x < w; x++){
            const uint8_t idx = (uint8_t)(OSData::frame->readPixelValue(x, y) & 0x0F);
            row[x >> 1] |= (x & 1) ? idx : (uint8_t)(idx << 4);
        }
        ok = (f.write(row, row_bytes) == row_bytes);
    }
    f.close();

    if(!ok){
        OSData::SD.remove(path);   //半端なファイルを残さない
        fail(out_path, out_size, "書き込みに失敗しました");
        return false;
    }

    LOG_SYS_OK("スクリーンショット保存: %s", path);
    if(out_path && out_size > 0) snprintf(out_path, out_size, "%s", path);
    next_no++;
    return true;
}
