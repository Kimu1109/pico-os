#include "services/App_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "system/I18n_Functions.hpp"

#include "apps/browser/MarkdownScene.hpp"
#include "gui/scenes/ClocksScene.hpp"
#include "apps/calculator/CalculatorScene.hpp"
#include "gui/scenes/FileExplorerScene.hpp"
#include "apps/texteditor/TextEditorScene.hpp"
#include "apps/notes/NotesScene.hpp"
#include "gui/scenes/SettingsScene.hpp"
#include "apps/dict/DictScene.hpp"
#include "apps/reader/ReaderLibraryScene.hpp"
#include "apps/calendar/CalendarScene.hpp"
#include "apps/chat/ChatScene.hpp"
#include "apps/todo/TodoScene.hpp"
#include "apps/mail/MailScene.hpp"
#include "apps/gb/GameBoyScene.hpp"
#include "apps/music/MusicScene.hpp"
#include "apps/video/VideoScene.hpp"
#include "apps/ssh/SshScene.hpp"
#include "gui/scenes/NotificationScene.hpp"
#include "apps/store/StoreScene.hpp"
#include "apps/store/App_Package.hpp"
#include "gui/scenes/UsbDriveScene.hpp"
#include "gui/scenes/FileShareScene.hpp"
#include "gui/scenes/RecorderScene.hpp"
#include "apps/firmware/FirmwareScene.hpp"
#include "lua/LuaAppScanner.hpp"
#include "services/UsbDrive_Functions.hpp"
#include "services/FileShare_Functions.hpp"
#include "apps/music/Music_Service.hpp"
#include "storage/SD_IO.hpp"
#include "config/Hardware_Config.hpp"
#include "config/App_Config.hpp"

// このOSに載せるアプリの一覧。
//
// アプリを増やすときは、シーンのヘッダをincludeして下の並びへ1行足すだけでよい。
// ビルド時に外せるアプリ(config/App_Config.hpp の PICO_APP_*)は、Register の行を `#if PICO_APP_*` で囲む(include はそのままでよい)。
// 並べた順にランチャへ表示される。
//
// 登録簿の仕組み(Register/Launch等)は App_Functions.cpp にあり、
// このファイルとは分けてある。仕組み側だけならシーンの実装に依存せずに
// ビルドできるので、ホストテストが軽く保てる。
//
// 生成関数は2種類:
//   MakeScene<T>        … 引数なしでシーンを作る
//   MakeSceneWithArg<T> … Register()の第4引数(arg)をコンストラクタへ渡す。
//                         同じシーン型を別のargで何度でも登録できるので、
//                         「文書ごとに1タイル」「Luaスクリプトごとに1タイル」が作れる
void AppFunctions::Setup(){
    Clear();

    // ---- ここへ1行足すとランチャに並ぶ ----
    //名前は日本語の原文のまま登録する(launcher.cfg・pico.launch_app・通知の送り主の識別子)。
    //TR_NOOP() は訳の一覧(i18n/)へ載せる印で、ランチャ等が見せるときに I18nFunctions::Tr() で訳す
    //引数を渡さないと network.cfg の browser-home を開く(無ければ同梱のサンプル)。
    //特定の文書を固定で開くタイルにしたい場合は MakeSceneWithArg + パス/URL を渡す
#if PICO_APP_BROWSER
    Register("Markdown",   IconID::Browser,  &MakeScene<MarkdownScene>);
#endif
    Register(TR_NOOP("時計"), IconID::Clock, &MakeScene<ClocksScene>);
#if PICO_APP_CALCULATOR
    Register(TR_NOOP("電卓"), IconID::Calculator, &MakeScene<CalculatorScene>);
#endif
    Register(TR_NOOP("ファイル"), IconID::Folder, &MakeScene<FileExplorerScene>);
    //ファイルの中身を見るビューワー(FileViewerScene)は、ファイルアプリでファイルを2回タップして開く
    //行単位のシンプルなテキストエディタ(TextEditorScene参照)
#if PICO_APP_TEXTEDITOR
    Register(TR_NOOP("テキスト"), IconID::Pencil, &MakeScene<TextEditorScene>);
#endif
    //SDの /notes/ のメモを一覧・#タグ・全文検索で引く。開くとテキストエディタ(NotesScene参照)
#if PICO_APP_NOTES
    Register(NotesScene::kAppName, IconID::Edit, &MakeScene<NotesScene>);
#endif
    Register(TR_NOOP("設定"), IconID::Settings, &MakeScene<SettingsScene>);
#if PICO_APP_DICT
    Register(TR_NOOP("辞書"), IconID::Language, &MakeScene<DictScene>);
#endif
    //電子書籍・長文(青空文庫のテキスト等)。ページ送り・しおり・続きから。本はSDの /books/ に置く
#if PICO_APP_READER
    Register(ReaderLibraryScene::kAppName, IconID::Book, &MakeScene<ReaderLibraryScene>);
#endif
#if PICO_APP_CALENDAR
    Register(TR_NOOP("カレンダー"), IconID::Calendar, &MakeScene<CalendarScene>);
#endif
    //自前のチャットサーバ(server/chat/)へ繋ぐ。接続先は /sys/chat.cfg
#if PICO_APP_CHAT
    Register(TR_NOOP("チャット"), IconID::Messages, &MakeScene<ChatScene>);
#endif
    //Todoistのタスク(TODO/リマインダー)。トークンは /sys/todoist.cfg(アプリの[設定]から入れる)
#if PICO_APP_TODO
    Register(TodoScene::kAppName, IconID::CheckboxOn, &MakeScene<TodoScene>);
#endif
    //メールの受信(IMAP)。アカウントは /sys/mail.cfg(アプリの[設定]から入れる)
#if PICO_APP_MAIL
    Register(MailScene::kAppName, IconID::Mail, &MakeScene<MailScene>);
#endif
    //Game Boy(DMG)エミュ。ROMはSDの /gb/ に置く(256KBまで。SUMMARY.md #9)
#if PICO_APP_GB
    Register(TR_NOOP("ゲームボーイ"), IconID::Game, &MakeScene<GameBoyScene>);
#endif
    //曲(pico-os MML)を並べて鳴らす。曲はSDの /music/ に置く(docs/MUSIC_FORMAT.md)。
    //音声出力を載せない版(config/Hardware_Config.hpp)には並べない(PICO_APP_MUSIC の既定が PICO_HW_SOUND。動画・ゲームボーイは音なしで動く)
#if PICO_APP_MUSIC
    Register(TR_NOOP("ミュージック"), IconID::Music, &MakeScene<MusicScene>);
#endif
#if PICO_APP_VIDEO
    Register(TR_NOOP("動画"), IconID::FileVideo, &MakeScene<VideoScene>);
#endif
    //マイク(SPH0645、GP26〜28)で /recordings/ へWAVを録る。マイクを載せない版には並べない
#if PICO_HW_MIC
    Register(RecorderScene::kAppName, IconID::Microphone, &MakeScene<RecorderScene>);
#endif
    //SSHクライアント(端末)。接続先は起動後に端末の中で聞く。鍵は /sys/ssh/id_ed25519(SshScene参照)
#if PICO_APP_SSH
    Register("SSH", IconID::Terminal, &MakeScene<SshScene>);
#endif
    //通知センター(ステータスバーのタップでも開く。NotificationFunctions参照)
    Register(TR_NOOP("通知"), IconID::Bell, &MakeScene<NotificationScene>);
    //アプリストア。配布元(docs/APP_REPO.md)からLuaアプリを取ってきて /lua/apps/ へ入れる
#if PICO_APP_STORE
    Register(StoreScene::kAppName, IconID::AppStore, &MakeScene<StoreScene>);
#endif
    //PCとUSBでつなぎ、SDカードをドライブとして見せる(SDの抜き差しをせずにファイルを行き来する)
    Register(UsbDriveScene::kAppName, IconID::SdCard, &MakeScene<UsbDriveScene>);
    //同じLANのブラウザからSDのファイルを読み書きする(開いている間だけ。docs/design/file-share.md)
    Register(FileShareScene::kAppName, IconID::Share, &MakeScene<FileShareScene>);
    // ファームの更新(SDの .pfw / PCからWi-Fiで。docs/FIRMWARE_UPDATE.md)
    Register(FirmwareScene::kAppName, IconID::Refresh, &MakeScene<FirmwareScene>);
    // Luaアプリは下のスキャンで登録する(動作確認用のサンプルは /lua/apps/動作確認/)。
    // 権限が要るものは各アプリの app.cfg に書く

    // "/lua/apps/<名前>/main.lua" を走査し、見つかった分をここまでの静的登録へ
    // 追加する(LuaAppScanner.hppのクラスコメント参照)。SD無し/ディレクトリが
    // 無い場合は何もしない。静的登録の後に置くことで、同名のLuaアプリが
    // SD側にもあった場合、後勝ちでSD側が優先される(登録簿は先勝ちではなく
    // 単純追記なので、実際には2タイル並ぶ点に注意。今のところ名前の重複チェックは
    // していない)
    LuaAppScanner::Scan();
    // SDの直下に置いたアプリのパッケージ(*.papp。Webのアプリストアから落としたもの)を入れる。
    // 入れたものはその場で登録簿へ入る。結果の通知は main.cpp が通知の Setup() の後で出す
    AppPackage::InstallPending();
    // USBドライブを終えてSDをマウントし直したら、PCで足した/消したアプリ・置いたパッケージを反映する
    // Wi-Fi転送で書き換えたときも同じ
    UsbDriveFunctions::sd_remounted = [](){
        LuaAppScanner::Scan();
        LuaAppScanner::PruneMissing();
        AppPackage::InstallPending();
        AppPackage::PostPendingNotice();
    };
    FileShareFunctions::sd_changed = UsbDriveFunctions::sd_remounted;
#if PICO_APP_MUSIC
    // 鳴らしている間は /music/ の中(と、/music/ を含むフォルダ)を書き換えさせない
    FileShareFunctions::path_busy = [](const char* p){
        return MusicService::Active() &&
               (PICO_IO::isSameOrInside(p, "/music") || PICO_IO::isSameOrInside("/music", p));
    };
#endif

    // ランチャの並べ替え・隠したアプリ(/sys/launcher.cfg)。登録を全部済ませた後に当てる
    LoadLayout();

    LOG_SYS_OK("App Setup has succeeded! (%d apps)", Count());
}
