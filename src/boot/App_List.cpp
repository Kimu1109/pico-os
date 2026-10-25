#include "services/App_Functions.hpp"
#include "system/Log_Functions.hpp"

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
#include "gui/scenes/UsbDriveScene.hpp"
#include "gui/scenes/RecorderScene.hpp"
#include "apps/firmware/FirmwareScene.hpp"
#include "lua/LuaAppScanner.hpp"
#include "services/UsbDrive_Functions.hpp"
#include "config/Hardware_Config.hpp"

// このOSに載せるアプリの一覧。
//
// アプリを増やすときは、シーンのヘッダをincludeして下の並びへ1行足すだけでよい。
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
    //引数を渡さないと network.cfg の browser-home を開く(無ければ同梱のサンプル)。
    //特定の文書を固定で開くタイルにしたい場合は MakeSceneWithArg + パス/URL を渡す
    Register("Markdown",   IconID::Browser,  &MakeScene<MarkdownScene>);
    Register("時計", IconID::Clock, &MakeScene<ClocksScene>);
    Register("電卓", IconID::Calculator, &MakeScene<CalculatorScene>);
    Register("ファイル", IconID::Folder, &MakeScene<FileExplorerScene>);
    //ファイルの中身を見るビューワー(FileViewerScene)は、ファイルアプリでファイルを2回タップして開く
    //行単位のシンプルなテキストエディタ(TextEditorScene参照)
    Register("テキスト", IconID::Pencil, &MakeScene<TextEditorScene>);
    //SDの /notes/ のメモを一覧・#タグ・全文検索で引く。開くとテキストエディタ(NotesScene参照)
    Register(NotesScene::kAppName, IconID::Edit, &MakeScene<NotesScene>);
    Register("設定", IconID::Settings, &MakeScene<SettingsScene>);
    Register("辞書", IconID::Language, &MakeScene<DictScene>);
    //電子書籍・長文(青空文庫のテキスト等)。ページ送り・しおり・続きから。本はSDの /books/ に置く
    Register(ReaderLibraryScene::kAppName, IconID::Book, &MakeScene<ReaderLibraryScene>);
    Register("カレンダー", IconID::Calendar, &MakeScene<CalendarScene>);
    //自前のチャットサーバ(server/chat/)へ繋ぐ。接続先は /sys/chat.cfg
    Register("チャット", IconID::Messages, &MakeScene<ChatScene>);
    //Todoistのタスク(TODO/リマインダー)。トークンは /sys/todoist.cfg(アプリの[設定]から入れる)
    Register(TodoScene::kAppName, IconID::CheckboxOn, &MakeScene<TodoScene>);
    //メールの受信(IMAP)。アカウントは /sys/mail.cfg(アプリの[設定]から入れる)
    Register(MailScene::kAppName, IconID::Mail, &MakeScene<MailScene>);
    //Game Boy(DMG)エミュ。ROMはSDの /gb/ に置く(256KBまで。SUMMARY.md #9)
    Register("ゲームボーイ", IconID::Game, &MakeScene<GameBoyScene>);
    //曲(pico-os MML)を並べて鳴らす。曲はSDの /music/ に置く(docs/MUSIC_FORMAT.md)。
    //音声出力を載せない版(config/Hardware_Config.hpp)には並べない(動画・ゲームボーイは音なしで動く)
#if PICO_HW_SOUND
    Register("ミュージック", IconID::Music, &MakeScene<MusicScene>);
#endif
    Register("動画", IconID::FileVideo, &MakeScene<VideoScene>);
    //マイク(SPH0645、GP26〜28)で /recordings/ へWAVを録る。マイクを載せない版には並べない
#if PICO_HW_MIC
    Register(RecorderScene::kAppName, IconID::Microphone, &MakeScene<RecorderScene>);
#endif
    //SSHクライアント(端末)。接続先は起動後に端末の中で聞く。鍵は /sys/ssh/id_ed25519(SshScene参照)
    Register("SSH", IconID::Terminal, &MakeScene<SshScene>);
    //通知センター(ステータスバーのタップでも開く。NotificationFunctions参照)
    Register("通知", IconID::Bell, &MakeScene<NotificationScene>);
    //アプリストア。配布元(docs/APP_REPO.md)からLuaアプリを取ってきて /lua/apps/ へ入れる
    Register(StoreScene::kAppName, IconID::AppStore, &MakeScene<StoreScene>);
    //PCとUSBでつなぎ、SDカードをドライブとして見せる(SDの抜き差しをせずにファイルを行き来する)
    Register(UsbDriveScene::kAppName, IconID::SdCard, &MakeScene<UsbDriveScene>);
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
    // USBドライブを終えてSDをマウントし直したら、PCで足した/消したアプリを反映する
    UsbDriveFunctions::sd_remounted = [](){
        LuaAppScanner::Scan();
        LuaAppScanner::PruneMissing();
    };

    // ランチャの並べ替え・隠したアプリ(/sys/launcher.cfg)。登録を全部済ませた後に当てる
    LoadLayout();

    LOG_SYS_OK("App Setup has succeeded! (%d apps)", Count());
}
