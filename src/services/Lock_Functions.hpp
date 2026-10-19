#pragma once

#include <cstdint>
#include <cstddef>

namespace KeyInputFunctions { struct Event; }

// 画面ロック(PIN)。Wi-Fiのパスワード・SSHの鍵・Todoistのトークン等を持つ端末なので、
// PINを決めておくと「起動したとき」と「スリープから起きたとき」にPINを聞く。
//
// - PINは数字4〜8桁。/sys/lock.cfg の pin = v1:<塩 16進32桁>:<ハッシュ 16進64桁>
//   (BLAKE2b-256。鍵はファームの固定鍵 PICO_Secret::kKey、塩は端末で作った乱数)。PINそのものは保存しない
//   端末の鍵(OTP。DeviceKeyFunctions)があれば v2:(鍵は端末の鍵)で作る。v1の記録は正しいPINを打ったときに v2 へ書き直す
// - 守れるのは「本体を手に取った人が画面を操作する」ことだけ。SDカードを抜いて読む・ファームを吸い出す
//   相手には無力(SDの中身は暗号化していない。暗号化してあるのはWi-Fi/SSH/チャット等の認証情報だけ)。
//   4桁のPINはハッシュから総当たりで分かる
// - 5回続けて間違えると30秒待たせ、その後は間違えるたびに倍(最大5分)。再起動で数え直す(起動時もロックする)
// - ロック中は最前面のオーバーレイ(LockScreen)が画面を全部覆い、タッチを全部取る。物理キーボードの打鍵は
//   KeyInputFunctions::intercept で、コントローラーは PadFunctions::SuppressThisFrame() で後ろの画面へ渡さない。
//   後ろの画面(SSHの接続・音楽・タイマー等)は動き続ける
// - アラーム/タイマーが鳴っていれば、ロック画面に「止める」を出す(ロックを解かずに止められる)
// - PINの設定/変更/解除もロック画面を使う(今のPINの確認 → 新しいPIN → もう一度)
namespace LockFunctions {

    constexpr int kMinPin = 4;
    constexpr int kMaxPin = 8;
    constexpr int kFreeTries = 5;
    constexpr unsigned long kFirstWaitMs = 30000;
    constexpr unsigned long kMaxWaitMs = 300000;

    // ロック画面が何を聞いているか
    enum class Step : uint8_t {
        None,       // 出していない
        Unlock,     // ロックを解く
        Verify,     // 変更/解除の前に今のPINを確かめる
        NewPin,     // 新しいPIN
        Confirm,    // 新しいPINをもう一度
    };

    enum class Check : uint8_t { Ok, Wrong, Wait, Invalid };

    // lock.cfg を読み、PINがあればロックした状態で始める。ロック画面のオーバーレイを作る(全オーバーレイの最後)
    void Setup();
    // loop()でPowerFunctions::Update()の直後に毎フレーム。スリープに入ったらロックし、ロック中は
    // コントローラーの入力を後ろの画面へ渡さない
    void Update();

    bool HasPin();
    bool IsLocked();
    // ロック画面を出しているか(ロック中、またはPINの変更の途中)
    bool IsShowing();
    Step CurrentStep();

    // 今すぐロックする(PINが無ければ何もしない)
    void LockNow();
    // PINの設定/変更(PINがあれば先に今のPINを聞く)・解除。ロック画面で聞く
    void BeginChange();
    void BeginRemove();
    // 変更/解除をやめる(ロックを解く途中ならやめられない)
    void Cancel();

    // ロック画面で打ったPINを渡す(今のStepに応じて確かめる/覚える)。結果の文言はMessage()
    Check Submit(const char* pin, unsigned long now_ms);
    const char* Message();
    bool MessageIsError();
    // 待たされている残り(ms)。0なら打てる
    unsigned long WaitRemainingMs(unsigned long now_ms);

    // ---- 中身(テスト用に出してある) ----
    bool IsValidPin(const char* pin);
    // PINと塩からハッシュ(32バイト)。deviceなら端末の鍵(OTP)、偽ならファームの固定鍵で
    void HashPin(const char* pin, const uint8_t salt[16], uint8_t out[32], bool device = false);
    // lock.cfgの値 "v1:<塩>:<ハッシュ>"(固定鍵)/"v2:..."(端末の鍵。あればこちらで作る)を作る/読む
    bool FormatRecord(const char* pin, const uint8_t salt[16], char* out, size_t cap);
    bool VerifyRecord(const char* record, const char* pin);
    // 保存せずにPINを入れ替える(テスト用。空ならPIN無し)
    void SetRecordForTest(const char* record);
    const char* CurrentRecordForTest();
    // 端末の鍵で作った記録(v2)か
    bool RecordUsesDeviceKey(const char* record);
    // PINがあり、まだ固定鍵の記録(v1)か(端末の鍵を作った後、次に正しく打ったときに v2 へ書き直す)
    bool RecordNeedsDeviceKeyUpgrade();

    // アラーム/タイマーが鳴っているか・止める(main.cppが差す。ロックをそれらへ依存させないため)
    inline bool (*ringing_query)() = nullptr;
    inline void (*ringing_stop)() = nullptr;

    // 打鍵(KeyInputFunctions::intercept)
    bool HandleKey(const KeyInputFunctions::Event& ev);
}
