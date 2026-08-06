#pragma once

#include "test/font_coverage_check.hpp"
#include "OS_Data.hpp"

namespace TestFunctions {
    inline void RunAll(){
        if(OSData::SD_usable){
            checkFontCoverage(OSData::SD, lgfxJapanGothicP_16, "dict/skk_body.tsv", "to-fu-chars.txt");
        }
    }
}