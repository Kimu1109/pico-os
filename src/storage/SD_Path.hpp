#pragma once

namespace PICO_Path {
    namespace DIR {
        constexpr const char* SYS = "/sys/";
        constexpr const char* SYS_IME = "/sys/ime/";
        constexpr const char* SYS_DICT = "/sys/dict/";
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

        namespace DICT {
            // script/en-ja-and-ja-en.tsv をそのままSDへ置く(検索用語句でソート済み)。
            // インデックスは script/build_dict_index.py が生成する。
            constexpr const char* DICT_BODY = "/sys/dict/en-ja-and-ja-en.tsv";
            constexpr const char* DICT_INDEX = "/sys/dict/dict_index.tsv";
            // 部分一致(語の途中一致)検索を高速化するサフィックスインデックス。
            // script/build_dict_suffix_index.py が生成する。無くても
            // WordDictionaryが全体走査へ自動フォールバックするため、
            // 置いていないSDでも壊れない。
            constexpr const char* DICT_SUFFIX_BODY = "/sys/dict/dict_suffixes.tsv";
            constexpr const char* DICT_SUFFIX_INDEX = "/sys/dict/dict_suffix_index.tsv";
        }
    };
};