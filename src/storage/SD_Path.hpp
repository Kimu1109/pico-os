#pragma once

namespace PICO_Path {
    namespace DIR {
        constexpr const char* SYS = "/sys/";
        constexpr const char* SYS_IME = "/sys/ime/";
        // Markdownブラウザがサーバから取った文書を貯める場所。
        // 配下は /cache/<ホスト>/<サーバ上のパス> でミラーする(PROTOCOL.md)
        constexpr const char* CACHE = "/cache/";
    };
    namespace FILE {
        constexpr const char* SYS_LOG_TXT = "/sys/log.txt";

        constexpr const char* TMP_TOFU_TXT = "/tmp/tofu-chars.txt";

        // キャッシュの目録(行指向TSV)。1行1文書
        constexpr const char* CACHE_INDEX_TSV = "/cache/index.tsv";

        namespace CFG {
            constexpr const char* SYS_NETWORK_CFG = "/sys/network.cfg";
            constexpr const char* SYS_USER_CFG = "/sys/user.cfg";
        }

        namespace IME {            
            constexpr const char* IME_SKK_BODY = "/sys/ime/skk_body.tsv";
            constexpr const char* IME_SKK_INDEX = "/sys/ime/skk_index.tsv";   
        }
    };
};