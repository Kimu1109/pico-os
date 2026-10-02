#include "functions/Screenshot_Functions.hpp"
#include "gui/icons/icon_render.h"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

#include <stdio.h>
#include <stdint.h>

namespace {
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
        snprintf(path, sizeof(path), "%s/shot_%04d.pimg", kDir, next_no);
        if(!OSData::SD.exists(path)) break;
        if(++next_no > 9999){ fail(out_path, out_size, "空き番号がありません"); return false; }
    }

    FsFile f = OSData::SD.open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if(!f){ fail(out_path, out_size, "ファイルを作れません"); return false; }

    //.pimg(4bpp+RLE)。パレットはファームの固定16色なのでファイルには含まれない
    const bool ok = IconRender::EncodePimg(*OSData::frame, SCREEN_WIDTH, SCREEN_HEIGHT, f);
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
