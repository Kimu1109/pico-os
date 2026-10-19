#include "apps/firmware/Fw_Version.hpp"

#include <cstdio>

#if defined(__has_include)
    #if __has_include("Pico_Build_Info.h")
        #include "Pico_Build_Info.h"
    #endif
#endif

#ifndef PICOOS_BUILD_SERIAL
    #define PICOOS_BUILD_SERIAL 0
#endif
#ifndef PICOOS_BUILD_GIT
    #define PICOOS_BUILD_GIT "dev"
#endif
#ifndef PICOOS_BUILD_DATE
    #define PICOOS_BUILD_DATE ""
#endif

#define PICOOS_STR2(x) #x
#define PICOOS_STR(x) PICOOS_STR2(x)

namespace {
    // script/firmware_tool.py が firmware.bin の中からこの並びを探す(書き方を変えるときは両方を直す)。
    // 参照されないとリンクで消えるので、Marker()から返す
    const char kMarker[] = "PICOOS-BUILD;serial=" PICOOS_STR(PICOOS_BUILD_SERIAL)
                           ";git=" PICOOS_BUILD_GIT ";date=" PICOOS_BUILD_DATE ";board=rpipico2w;";
}

uint32_t FwVersion::Serial(){ return (uint32_t)PICOOS_BUILD_SERIAL; }
const char* FwVersion::Git(){ return PICOOS_BUILD_GIT; }
const char* FwVersion::Date(){ return PICOOS_BUILD_DATE; }
const char* FwVersion::Marker(){ return kMarker; }

const char* FwVersion::Text(){
    static char text[48];
    if(!text[0]) snprintf(text, sizeof(text), "#%lu (%s)", (unsigned long)Serial(), Git());
    return text;
}
