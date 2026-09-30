// 通知(NotificationFunctions)の「外とのつなぎ」: 実際の時刻・電池・Wi-Fiの取り込み、トーストの出し入れ、
// 通知音、タップで起動するアプリの登録簿との接続。
// 中身(予約・履歴・条件の判定)は Notification_Functions.cpp にあり、ホストテストはそちらだけを動かす。
#include "functions/Notification_Functions.hpp"
#include "functions/App_Functions.hpp"
#include "functions/Battery_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "functions/Network_Functions.hpp"
#include "functions/Power_Functions.hpp"
#include "functions/Scene_Functions.hpp"
#include "functions/Sound_Functions.hpp"
#include "functions/Time_Functions.hpp"
#include "functions/Widget_Functions.hpp"
#include "gui/scenes/NotificationScene.hpp"
#include "gui/widgets/systems/NotificationToast.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <ctime>

namespace {
    using namespace NotificationFunctions;

    // トーストを出しておく時間。後ろに順番待ちがあれば短くして回す
    constexpr unsigned long kToastMs = 5000;
    constexpr unsigned long kToastQueuedMs = 2500;
    // 通知音(2音)。2音目は1音目の後に鳴らす
    constexpr unsigned long kChimeGapMs = 110;

    NotificationToast* toast = nullptr;
    unsigned long chime2_at = 0;
    bool chime2_pending = false;

    bool Launch(const char* app_name){
        return AppFunctions::LaunchByName(app_name);
    }

    // 予約の送り主(Luaアプリのディレクトリ)に当たるアプリを登録簿から探す
    bool ResolveOwner(const char* owner, AppName& out_app){
        return AppFunctions::NameForDir(owner, out_app);
    }

    bool QuietNow(){
        if(GetMode() == Mode::Quiet) return true;
        Scene* s = SceneFunctions::Current();
        return s && s->quietNotifications();
    }

    void PlayChimeNote(uint32_t freq, uint16_t ms){
        ChipSynth::Note n;
        n.wave = ChipSynth::Wave::Pulse25;
        n.freq_x16 = freq * 16u;
        n.volume = 12;
        n.envelope = -3;
        n.length_ms = ms;
        //曲が使いにくい最後のチャンネルを借りる(Play()は曲からチャンネルを借りるだけで、曲は進み続ける)
        SoundFunctions::Play((uint8_t)(SoundFunctions::kChannels - 1), n);
    }

    void UpdateChime(unsigned long now_ms, bool quiet){
        if(TakeChime() && !quiet){
            PlayChimeNote(1319, 90);   // E6
            chime2_pending = true;
            chime2_at = now_ms + kChimeGapMs;
        }
        if(chime2_pending && (long)(now_ms - chime2_at) >= 0){
            chime2_pending = false;
            PlayChimeNote(1760, 160);  // A6
        }
    }

    void UpdateToast(unsigned long now_ms, bool quiet){
        if(!toast) return;

        if(quiet){
            //控える画面の間は出さない(順番待ちも捨てる。履歴と印には残っている)
            uint32_t seq;
            while(PopToast(seq)){}
            toast->hide();
            return;
        }

        if(toast->isShowing()){
            //見せている間はスリープさせない(スリープ中に来た通知で画面を起こす)
            PowerFunctions::KeepAwake();
            const Entry* e = FindEntry(toast->getSeq());
            const unsigned long limit = (PendingToastCount() > 0) ? kToastQueuedMs : kToastMs;
            //消された/既読にされた(通知センターで見た)ら引っ込める
            bool expired = !e || e->read || now_ms - toast->getShownMs() >= limit;
            if(!expired) return;
            toast->hide();
        }

        uint32_t seq;
        if(PopToast(seq)){
            const Entry* e = FindEntry(seq);
            if(e && !e->read){
                toast->show(*e, now_ms);
                PowerFunctions::KeepAwake();
            }
        }
    }
}

void NotificationFunctions::Setup(){
    SetupAt(millis());
    SetLauncher(&Launch);
    //アンインストールされたLuaアプリの予約を捨て、アプリ名を登録簿に合わせる(AppFunctions::Setup()より後)
    RefreshOwners(&ResolveOwner);

    toast = new NotificationToast();
    //オーバーレイの一番上(キーボードや半透明のダイアログの上にも出す)。キーボードより後にSetup()すること
    WidgetFunctions::AddOverlay(toast);
    toast->setOnPressEnd([](){
        if(!toast || !toast->isShowing()) return;
        //指を離した位置で決める(トーストの外で離したら何もしない)
        const int x = OSData::touchX, y = OSData::touchY;
        if(!toast->contains(x, y)) return;
        const uint32_t seq = toast->getSeq();
        toast->hide();
        if(toast->hitClose(x, y)){
            MarkRead(seq);
            return;
        }
        //離れると状態を失う画面(SSH・ゲームボーイ)では移らず、既読にするだけ
        Scene* cur = SceneFunctions::Current();
        if(cur && cur->keepForeground()){
            MarkRead(seq);
            return;
        }
        //送ったアプリを開く。開けなければ(OSからの通知等)通知センターを開く
        if(!Open(seq)) OpenCenter();
    });

    LOG_SYS_OK("Notification Setup has succeeded! (予約%d件)", RuleCount());
}

void NotificationFunctions::Update(){
    const unsigned long now_ms = millis();
    Sensors s;
    s.battery_valid = BatteryFunctions::HasSample();
    s.battery_percent = s.battery_valid ? BatteryFunctions::GetPercent() : 100;
    s.wifi_connected = NetworkFunctions::IsConnected();
    UpdateAt(now_ms, TimeFunctions::timeinfo, (int64_t)time(nullptr), s);

    const bool quiet = QuietNow();
    UpdateChime(now_ms, quiet);
    UpdateToast(now_ms, quiet);
}

void NotificationFunctions::OpenCenter(){
    Scene* cur = SceneFunctions::Current();
    if(cur && strcmp(cur->getName(), NotificationScene::kName) == 0) return;
    //離れると状態を失う画面(SSH・ゲームボーイ)からは移らない(ステータスバーをうっかり触っても接続/ゲームを失わない)
    if(cur && cur->keepForeground()) return;
    SceneFunctions::Push(new NotificationScene());
}
