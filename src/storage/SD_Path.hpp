#pragma once

namespace PICO_Path {
    namespace DIR {
        constexpr const char* SYS = "/sys/";
        constexpr const char* SYS_IME = "/sys/ime/";
    };
    namespace FILE {
        constexpr const char* SYS_NETWORK_CFG = "/sys/network.cfg";
        constexpr const char* SYS_LOG_TXT = "/sys/log.txt";

        constexpr const char* IME_SKK_BODY = "/sys/ime/skk_body.tsv";
        constexpr const char* IME_SKK_INDEX = "/sys/ime/skk_index.tsv";
    };
};