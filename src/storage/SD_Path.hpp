#pragma once

namespace PICO_Path {
    namespace DIR {
        constexpr const char* SYS = "/sys/";
        constexpr const char* SYS_IME = "/sys/ime/";
        constexpr const char* SYS_DICT = "/sys/dict/";
        // Markdownブラウザがサーバから取った文書を貯める場所。
        // 配下は /cache/<ホスト>/<サーバ上のパス> でミラーする(PROTOCOL.md)
        constexpr const char* CACHE = "/cache/";
        // ランチャへ自動登録されるLuaアプリの置き場所(LuaAppScanner参照)。
        // 直下の「サブディレクトリ1つ = アプリ1つ」で、<名前>/main.lua が
        // あればその<名前>をそのままタイル名として登録する。
        // 動作サンプル(hello.lua等)を置く /lua/ 直下とは別にしてあるのは、
        // "/lua/"直下を走査するとhello_sub.lua等のサブ画面スクリプトまで
        // 誤って1タイルずつ登録してしまうため
        constexpr const char* LUA_APPS = "/lua/apps/";
        // カレンダーアプリが読む .ics の置き場所。直下の *.ics を全部読んで1つに重ねる
        // (Googleのカレンダーごとの非公開URLを1ファイルずつ置く想定。CalendarScene参照)
        constexpr const char* CALENDAR = "/calendar/";
        // Game Boyエミュ(GameBoyScene)がROM選択の最初に開く場所。
        // セーブ(.sav)はROMと同じ場所に「拡張子だけ変えた名前」で置く
        constexpr const char* GB_ROMS = "/gb/";
    };
    namespace FILE {
        constexpr const char* SYS_LOG_TXT = "/sys/log.txt";

        constexpr const char* TMP_TOFU_TXT = "/tmp/tofu-chars.txt";

        // キャッシュの目録(行指向TSV)。1行1文書
        constexpr const char* CACHE_INDEX_TSV = "/cache/index.tsv";

        // HTTPSで追加で信頼するルート証明書(PEM、複数連結可)。
        // 焼き込みのルート(net/Tls_Roots_Data.hpp)に無い相手や、自己署名の自前サーバ向け
        constexpr const char* TLS_EXTRA_CA_PEM = "/sys/tls/ca.pem";

        // カレンダーの取得元の一覧("名前 = URL" の行)。CalendarSync参照
        constexpr const char* CALENDAR_SOURCES = "/calendar/sources.cfg";

        namespace CFG {
            constexpr const char* SYS_NETWORK_CFG = "/sys/network.cfg";
            constexpr const char* SYS_USER_CFG = "/sys/user.cfg";
            // チャットアプリの接続先("server = https://..." と "token = ...")。ChatClient参照
            constexpr const char* SYS_CHAT_CFG = "/sys/chat.cfg";
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