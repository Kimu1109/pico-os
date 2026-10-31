#pragma once

#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// Wi-Fi転送: 同じLANのブラウザ・curlから、SDのファイルを一覧・ダウンロード・アップロード・削除・名前の変更・
// フォルダ作成する(画面は FileShareScene、取り決めと守りは net/File_Share、設計は docs/design/file-share.md)。
//
//   Start()  … 待ち受けを始め、毎回ちがうキー(8文字)を作る。画面を開いたとき
//   Update() … 画面が開いている間、毎フレーム。kBudgetMs の間だけ読む・SDへ書く・送るを回して戻る
//   Stop()   … 待ち受けをやめる。途中のアップロードは捨てる(<パス>.part を消す)。
//              何か書き換えていたら sd_changed() を呼ぶ(Luaアプリ・パッケージをランチャへ反映する)
//
// - SDに触るのは1コア目の loop() だけなので、USBドライブのようにSDを取り上げはしない。
//   OSData::SD_usable が偽の間は 503 で断る
// - 接続は同時に1本(ページのJSが1つずつ送る)。来た順に相手をする
// - アップロードは <パス>.part へ書き、最後まで届いたら置き換える(途中で切れたら消す)
// - 画面ロックの間は要求を受けない(423)
// - SetDirect() で本体がAPになれる(家のWi-Fiが無い所で、スマホと本体だけで)
// - サーバの持ち物(要求の見出し・送受の溜め場所、約9KB)は Start() で new し Stop() で返す
// PCビルドは http://127.0.0.1:8082/ で開ける(PICOOS_SHARE_PORT で変更。母艦の外からは開けない)。Webビルドでは使えない。
namespace FileShareFunctions {
    constexpr const char* kAppName = "Wi-Fi転送";  // 登録簿の名前(FileShareScene::kAppName)
    constexpr uint16_t kHttpPort = 80;
    constexpr uint32_t kBudgetMs = 12;          // 1フレームで転送に使う時間
    constexpr uint32_t kIdleTimeoutMs = 10000;  // 何も届かない・送れないまま待つ上限
    constexpr size_t   kIoBytes = 4096;         // SDとの間の溜め場所(セクタ8つ)

    enum class State : uint8_t { Idle, Running };

    // 今の転送(画面の表示用)
    enum class Op : uint8_t { None, Upload, Download };
    struct Activity {
        Op op = Op::None;
        FixedString<PICO_STR_L> name;   // ファイル名だけ
        uint64_t done = 0;
        uint64_t total = 0;
    };
    constexpr int kRecentCount = 4;

    bool Start(FixedString<PICO_STR_LL>& err);
    void Update();
    void Stop();

    // 直接つなぐ: 本体がWi-Fiのアクセスポイント(「<名前>-share」、パスワードは毎回ちがう8桁の数字)になり、
    // 家のWi-Fiが無い所でもスマホと本体だけで使う。切ると保存済みのWi-Fiへの自動接続へ戻る。
    // 途中の転送はやめる。APのアドレスは 192.168.42.1(arduino-pico)。DNSには答えない
    // (答えるとスマホが「ログインが要るネットワーク」の小さなブラウザで開き、そこではファイルの選択・保存ができない)
    bool SetDirect(bool on, FixedString<PICO_STR_LL>& err);
    bool IsDirect();
    const char* ApSsid();
    const char* ApPassword();
    int ApClients();   // APに繋いでいる端末の数

    State GetState();
    // 開くURL("http://picoos.local/")と、IPのURL("http://192.168.1.23/"。Wi-Fiに繋がっていなければ空)
    void PageUrls(FixedString<PICO_STR_L>& by_name, FixedString<PICO_STR_L>& by_ip);
    // キーを付けたURL(QRに載せる)。IPを優先する(スマホが .local を引けないことがあるため)
    void KeyUrl(FixedString<PICO_STR_LL>& out);
    const char* Key();
    const Activity& Current();
    // 最近の操作(新しい順、i < RecentCount())
    int RecentCount();
    const char* Recent(int i);
    // 表示を描き直す合図(操作が終わるたびに増える)
    uint32_t Revision();

    // そのパスを今書き換えると困るか(ミュージックで鳴らしている曲のフォルダ等)。boot/ が差す。nullptrなら困らない
    extern bool (*path_busy)(const char* path);
    // Stop() のとき、何か書き換えていたら呼ぶ。boot/ が差す
    extern void (*sd_changed)();
    // ファイルを1つ受け取った・消した・動かした直後に呼ぶ(動かしたときは元と先の両方)。boot/ が差す(Luaのホットリロード)
    extern void (*file_changed)(const char* path);

    // 画面を閉じても待ち受けを続けるか(Luaのホットリロード。lua/LuaHotReload)。続けている間は
    // 画面の代わりに LuaHotReload の仕事が Update() を呼ぶ。Stop() で偽に戻る
    void SetBackground(bool on);
    bool InBackground();
}
