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
        // アプリのフォルダの中の main.lua 以外(push_scene で開くページ・require するモジュール)は
        // タイルにならない
        constexpr const char* LUA_APPS = "/lua/apps/";
        // カレンダーアプリが読む .ics の置き場所。直下の *.ics を全部読んで1つに重ねる
        // (Googleのカレンダーごとの非公開URLを1ファイルずつ置く想定。CalendarScene参照)
        constexpr const char* CALENDAR = "/calendar/";
        // Game Boyエミュ(GameBoyScene)がROM選択の最初に開く場所。
        // セーブ(.sav)はROMと同じ場所に「拡張子だけ変えた名前」で置く
        constexpr const char* GB_ROMS = "/gb/";
        // Game Boyエミュの途中保存(ステートセーブ)の置き場所。"<ROMの名前>.st0"(自動)/".st1"(手動)。
        // ROMの隣に置くとROMの選択に並んで紛らわしいので分けた(同じ名前の別ROMは見出しの照合で断る)
        constexpr const char* GB_STATES = "/gb/states/";
        // 曲(pico-os MML、MUSIC_FORMAT.md)の置き場所。ミュージックアプリ(MusicScene)が直下と1段下のフォルダ(=プレイリスト)の *.mml / *.wav を並べる
        constexpr const char* MUSIC = "/music/";
        // 動画(.pvid、VIDEO_FORMAT.md)の置き場所。動画アプリ(VideoScene)のファイル選択の最初の場所。音は同じ名前の .wav
        constexpr const char* VIDEO = "/video/";
    };
    namespace FILE {
        constexpr const char* SYS_LOG_TXT = "/sys/log.txt";
        // log.txtが大きくなったとき、起動時に1世代だけ残す先
        constexpr const char* SYS_LOG_OLD_TXT = "/sys/log.old.txt";

        constexpr const char* TMP_TOFU_TXT = "/tmp/tofu-chars.txt";

        // キャッシュの目録(行指向TSV)。1行1文書
        constexpr const char* CACHE_INDEX_TSV = "/cache/index.tsv";

        // HTTPSで追加で信頼するルート証明書(PEM、複数連結可)。
        // 焼き込みのルート(net/Tls_Roots_Data.hpp)に無い相手や、自己署名の自前サーバ向け
        constexpr const char* TLS_EXTRA_CA_PEM = "/sys/tls/ca.pem";

        // カレンダーの取得元の一覧("名前 = URL" の行)。CalendarSync参照
        constexpr const char* CALENDAR_SOURCES = "/calendar/sources.cfg";

        // SSHアプリ(SshScene)。公開鍵認証に使う秘密鍵(OpenSSH形式・ssh-ed25519・パスフレーズ無し)と、
        // 信頼したホスト鍵の一覧(OpenSSHのknown_hostsと同じ書き方)
        constexpr const char* SSH_ID_ED25519 = "/sys/ssh/id_ed25519";
        constexpr const char* SSH_KNOWN_HOSTS = "/sys/ssh/known_hosts";

        // 通知の予約(1行1件のタブ区切り)。NotificationFunctions参照
        constexpr const char* SYS_NOTIFY_RULES = "/sys/notify_rules.tsv";

        // アプリストア(store/App_Store、APP_REPO.md)。確かめたカタログ・最後に見た版の番号・
        // インストールしたアプリのファイル一覧(installed/<id>.tsv)・取得中の置き場(staging/)
        constexpr const char* STORE_DIR = "/sys/store";
        constexpr const char* STORE_CATALOG = "/sys/store/catalog.tsv";
        constexpr const char* STORE_CATALOG_PART = "/sys/store/catalog.part";
        constexpr const char* STORE_STATE = "/sys/store/state.cfg";
        constexpr const char* STORE_INSTALLED_DIR = "/sys/store/installed";
        constexpr const char* STORE_STAGING = "/sys/store/staging";
        constexpr const char* STORE_STAGING_FILES = "/sys/store/staging/files.tsv";
        constexpr const char* STORE_STAGING_APP = "/sys/store/staging/app";

        namespace CFG {
            constexpr const char* SYS_NETWORK_CFG = "/sys/network.cfg";
            // 保存済みのWi-Fiネットワークと Wi-FiのON/OFF("enabled" / "ssid1" / "pass1" ...)。net/Wifi_Profiles参照
            constexpr const char* SYS_WIFI_CFG = "/sys/wifi.cfg";
            constexpr const char* SYS_USER_CFG = "/sys/user.cfg";
            // チャットアプリの接続先("server = https://..." と "token = ...")。ChatClient参照
            constexpr const char* SYS_CHAT_CFG = "/sys/chat.cfg";
            // アプリストアの取得元("url = https://.../apps/")と信頼する公開鍵("key = 16進64桁"、何行でも)
            constexpr const char* SYS_STORE_CFG = "/sys/store.cfg";
            // TODOアプリ(Todoist)のトークン("token = enc1:...")と表示の設定。TodoistClient参照
            constexpr const char* SYS_TODOIST_CFG = "/sys/todoist.cfg";
            constexpr const char* SYS_CALENDAR_CFG = "/sys/calendar.cfg";
            constexpr const char* SYS_LOCK_CFG = "/sys/lock.cfg";
            // 音声出力("output = auto | off" / "volume = 0〜100")。SoundFunctions参照
            constexpr const char* SYS_SOUND_CFG = "/sys/sound.cfg";
            // ミュージックアプリの再生のしかた("shuffle = true|false" / "repeat = off|all|one")。MusicScene参照
            constexpr const char* SYS_MUSIC_CFG = "/sys/music.cfg";
            // 画面の明るさ("brightness = 0〜100" / "auto-dim = true|false")。DisplayFunctions参照
            constexpr const char* SYS_DISPLAY_CFG = "/sys/display.cfg";
            // タッチ位置の補正("a"〜"f"の6つの小数)。TouchCalibFunctions参照
            constexpr const char* SYS_TOUCH_CFG = "/sys/touch.cfg";
            // SSHアプリ("target = user@host:port" / "font = small|large")。SshScene参照
            constexpr const char* SYS_SSH_CFG = "/sys/ssh.cfg";
            // アラーム("alarm1 = 07:30,daily,on" ...)。AlarmFunctions参照
            constexpr const char* SYS_ALARM_CFG = "/sys/alarm.cfg";
            // 通知の見せ方("mode = on | quiet" / "sound = true | false")。NotificationFunctions参照
            constexpr const char* SYS_NOTIFY_CFG = "/sys/notify.cfg";
            // 開発者向けの道具("perf-overlay" / "perf-log" / "lua-debugger" / "watchdog" / "watchdog-ms")。DevToolsFunctions参照
            constexpr const char* SYS_DEBUG_CFG = "/sys/debug.cfg";
            // ランチャの並びと隠したアプリ("app = 名前" / "hidden = 名前" を上から順)。AppFunctions::LoadLayout()参照
            constexpr const char* SYS_LAUNCHER_CFG = "/sys/launcher.cfg";
            // バックグラウンドの仕事の入り切り("chat = on|off" / "lua:<アプリのフォルダ名> = on|off")。BackgroundFunctions参照
            constexpr const char* SYS_BACKGROUND_CFG = "/sys/background.cfg";
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