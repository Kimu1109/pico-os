#pragma once

#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_Path.hpp"
#include "test/font_coverage_check.hpp"
#include "OS_Data.hpp"

namespace TestFunctions {
    inline void RunAll(){
        LOG_SYS_OK("Test Service: All of Tests begin!");
        if(OSData::SD_usable){
            checkFontCoverage(OSData::SD, lgfxJapanGothicP_16, PICO_Path::FILE::IME::IME_SKK_BODY, PICO_Path::FILE::TMP_TOFU_TXT);
        }
        LOG_SYS_OK("Test Service: All of Tests has done!");
    }
    inline void Setup(){
        bool is_ok = PICO_Config::ParseFile(PICO_Path::FILE::CFG::SYS_USER_CFG,
            [&](const char* key, const char* value){
                if(strcmp(key, "run-test") == 0){
                    bool run_test = false;
                    if(PICO_Config::ConfigValue::AsBool(value, run_test) && run_test){
                        RunAll();
                    }
                }
            }
        );
        if(is_ok){
            LOG_SYS_OK("Test Setup has succeeded!");
        }else{
            LOG_SYS_FAIL("Test Setup: Failed to read a config file : %s", PICO_Path::FILE::CFG::SYS_USER_CFG);
        }
    }
}