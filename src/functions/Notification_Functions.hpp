#pragma once
#include <cstdint>
#include <ctime>

#include "util/FixedString.hpp"
#include "consts.hpp"

// 通知。アプリを開いていなくても、時間が経った・決まった時刻になった・電池が減った等の
// 条件で知らせる。条件を見張るのはOS(main.cppのloop())で、アプリ(Luaを含む)は「予約する」だけ。
// Luaアプリは閉じるとLuaEngineごと消えるので、Lua側のコードで条件を判定することはできない
// (任意のLuaで判定する条件は今のところ対象外)。
//
//   pico.notify{...} / C++のPost()/Schedule() ─▶ 予約(kMaxRules件の固定長テーブル)
//                                                  │ UpdateAt()が毎フレーム条件を見る
//                                                  ▼
//                                  履歴(kMaxHistory件の輪)へ積む + トーストの列 + 音
//                                                  │ トースト/通知センターでタップ
//                                                  ▼
//                                  Open(): 送ったアプリを起動し、起動理由(tag/data)を渡す
//
// - 予約の種類(Trigger):
//     Delay  … 今からN ms後(millis()の差分。NTPで時計が飛んでも、スリープしても狂わない)。再起動で消える
//     At     … 決まった日時(エポック秒)。NTP同期前(2020年より前)は待つ。過ぎていたら同期した時点で出す
//     Daily  … 毎日HH:MM。アラームと同じく同じ分に2回出さない
//     Every  … N ms ごと(kMinEveryMs以上)
//     BatteryLow       … 電池がN%を下回ったとき(戻る(N+5%以上)まで次は出さない)
//     WifiConnected / WifiDisconnected … Wi-Fiがつながった/切れたとき
//   Delay/Atは1回出したら消える。それ以外は取り消すまで残る。
// - 予約は /sys/notify_rules.tsv に保存する(Delay以外。変更から kSaveDelayMs 後にまとめて書く)。
//   1行1件のタブ区切り(値が長くConfig_Functionsの上限(160B)に収まらないため自前の書式)。
// - 同じ送り主(owner)・同じtagの予約/通知は置き換える(二重登録を防ぐ)。
// - 送り主1つあたりの予約はkMaxRulesPerOwner件まで(Luaアプリが表を埋め尽くさないため)。
// - 履歴(通知センターに並ぶもの)はRAMだけで持つ(再起動で消える)。
// - 見せ方の設定は /sys/notify.cfg(無くてよい): `mode = on | quiet`、`sound = true | false`。
//   quiet(控えめ)はトーストも音も出さず、ステータスバーの印と通知センターだけにする。
//
// このファイル(と.cpp)は描画・Wi-Fi・電池に依存しない中身だけ。トーストの表示、音、
// 実際のWi-Fi/電池の状態の取り込みは Notification_Sources.cpp(Setup()/Update())が受け持つ
// (ホストテストでは中身だけをUpdateAt()で動かす)。
namespace NotificationFunctions {

    constexpr int kMaxRules = 16;
    constexpr int kMaxRulesPerOwner = 4;
    constexpr int kMaxHistory = 16;
    constexpr int kMaxToastQueue = 8;
    constexpr unsigned long kMinEveryMs = 10000;
    constexpr unsigned long kSaveDelayMs = 1000;
    constexpr int kBatteryRearmMargin = 5;
    // NTP同期前(1970年付近)は時刻が当てにならないので、時刻の予約は出さない
    constexpr int kMinValidYear = 2020;
    // 起動理由(TakeLaunchReason)を受け取れる猶予。起動に失敗した理由が後で別の起動に混ざらないように
    constexpr unsigned long kLaunchReasonTtlMs = 3000;

    using Title   = FixedString<PICO_STR_M>;   // 48B(日本語16文字)
    using Body    = FixedString<PICO_STR_L>;   // 96B(日本語32文字)
    using Tag     = FixedString<PICO_STR_S>;   // 24B
    using Owner   = FixedString<PICO_STR_L>;   // Luaアプリのディレクトリ("/lua/apps/テトリス")。C++は空
    using AppName = FixedString<PICO_STR_M>;   // タップで起動するアプリ(登録簿の名前)。空なら起動しない

    enum class Trigger : uint8_t {
        Now = 0,   // 予約せずにすぐ出す(Post())
        Delay, At, Daily, Every, BatteryLow, WifiConnected, WifiDisconnected
    };
    // 保存・Luaの notify_list() で使う名前
    const char* TriggerToStr(Trigger t);

    // 通知の中身。文字列は Sanitize() でタブ/改行を空白にしてから入れる(保存の書式が崩れないように)
    struct Content {
        Title   title;
        Body    body;
        Tag     tag;
        Tag     data;      // 起動したアプリへ渡す値(Luaの pico.launch_reason())
        AppName app;
        Owner   owner;
        bool    sound = true;
    };

    // いつ出すか
    struct When {
        Trigger       trigger    = Trigger::Now;
        unsigned long delay_ms   = 0;   // Delay
        unsigned long every_ms   = 0;   // Every
        int64_t       at_epoch   = 0;   // At
        uint8_t       hour       = 0;   // Daily
        uint8_t       minute     = 0;
        uint8_t       below      = 15;  // BatteryLow(%)
    };

    struct Rule {
        uint16_t id = 0;         // 0 = 空き
        When     when;
        Content  content;
        // 実行中の状態(保存しない)
        unsigned long due_ms   = 0;     // Delay/Every: 次に出すmillis()
        long     last_minute_key = -1;  // Daily: 同じ分に2回出さない
        bool     armed         = true;  // BatteryLow: 下回ったら出し、戻るまでfalse
        bool     has_last      = false; // Wifi*: 前回の状態を知っているか
        bool     last_state    = false;
    };

    // 履歴(通知センター)の1件
    struct Entry {
        uint32_t      seq = 0;    // 0 = 空き。1から増える通し番号
        Content       content;
        int64_t       epoch = 0;  // 出した時刻(時計が合っていなければ0)
        unsigned long ms = 0;     // 出したmillis()
        bool          read = false;
    };

    // 条件の判定に使う外の状態(UpdateAt()へ渡す)
    struct Sensors {
        bool battery_valid = false;
        int  battery_percent = 100;
        bool wifi_connected = false;
    };

    enum class Mode : uint8_t { On = 0, Quiet };

    enum class Result : int8_t {
        Ok = 0,
        Full,          // 予約の表が満杯
        OwnerQuota,    // この送り主の予約が上限
        Invalid,       // 引数がおかしい(タイトルが空、Everyが短すぎる等)
    };
    const char* ResultToStr(Result r);

    // タブ/改行(保存の区切り)を空白にしてから入れる。切り詰めずに収まったらtrue
    bool Sanitize(FixedString<PICO_STR_S>& out, const char* s);
    bool Sanitize(FixedString<PICO_STR_M>& out, const char* s);
    bool Sanitize(FixedString<PICO_STR_L>& out, const char* s);

    // ===== 本体 =====
    void Setup();   // notify.cfg/予約を読み、トーストを作る(Notification_Sources.cpp。SDとAppFunctionsより後)
    void Update();  // loop()から毎フレーム(同上)
    // 通知センター(NotificationScene)を開く(ステータスバーのタップ等。開いていれば何もしない。
    // 離れると状態を失う画面(Scene::keepForeground())からは開かない)
    void OpenCenter();

    // すぐ出す。履歴の通し番号を返す(0なら出せなかった=タイトルが空)
    uint32_t Post(const Content& c);
    // 予約する。成功したら予約のid(1以上)をout_idへ。Trigger::Nowなら予約せずPost()する(out_idは0)
    Result Schedule(const Content& c, const When& w, uint16_t* out_id = nullptr);

    // 取り消し。ownerが違う予約には触らない(Luaアプリが他のアプリの予約を消せないように)。消した件数を返す
    int Cancel(const char* owner, uint16_t id);
    int CancelTag(const char* owner, const char* tag);
    int CancelAll(const char* owner);
    // 通知センターから(送り主を問わず)1件取り消す
    bool CancelById(uint16_t id);

    // 予約の一覧(空きを除いて前から詰めて数える)
    int RuleCount();
    const Rule* RuleAt(int index);
    const Rule* FindRule(uint16_t id);

    // 履歴。index 0 が一番新しい
    int HistoryCount();
    const Entry* HistoryAt(int index);
    const Entry* FindEntry(uint32_t seq);
    int UnreadCount();
    void MarkRead(uint32_t seq);
    void MarkAllRead();
    void RemoveEntry(uint32_t seq);
    void ClearHistory();
    // 中身が変わるたびに増える(通知センター/ステータスバーの描き直しの目安)
    uint32_t Revision();

    // トーストで見せる順番待ち。見せたら取り出す(履歴に残っていなければ飛ばす)
    bool PopToast(uint32_t& out_seq);
    // 順番待ちの数(後ろに待っていれば今のトーストを早めに引っ込める)
    int PendingToastCount();
    // 鳴らすべき通知音が溜まっているか(取り出すと下りる)
    bool TakeChime();

    // タップで開く: 既読にし、送ったアプリを起動して起動理由を渡す。起動できるアプリが無ければfalse
    bool Open(uint32_t seq);
    // 起動されたアプリ(LuaScene)が受け取る。ownerが一致し、kLaunchReasonTtlMs以内のものだけ(1回きり)
    bool TakeLaunchReason(const char* owner, Tag& out_tag, Tag& out_data);

    Mode GetMode();
    void SetMode(Mode m, bool persist = true);
    bool GetSoundEnabled();
    void SetSoundEnabled(bool on, bool persist = true);

    // 登録簿を見て、予約の送り主がまだいるかを確かめ、アプリ名を最新にする。
    // resolveがfalseを返した送り主(アンインストールされたLuaアプリ)の予約は消す
    void RefreshOwners(bool (*resolve)(const char* owner, AppName& out_app));

    // ===== テスト用(時刻/状態を外から与える) =====
    void SetupAt(unsigned long now_ms);        // 表を空にし、SDから読み直す(トーストは作らない)
    void UpdateAt(unsigned long now_ms, const struct tm& now, int64_t now_epoch, const Sensors& s);
    void SaveNow();                            // 保存を待たずに書く
    // 起動の差し替え(既定はAppFunctions::LaunchByName)
    void SetLauncher(bool (*launch)(const char* app_name));
}
