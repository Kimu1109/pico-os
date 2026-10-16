#pragma once

#include <cstddef>
#include <cstdint>

#include "firmware/Fw_Update.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// ファームの更新(FIRMWARE_UPDATE.md)のOSへのつなぎ。中身の流れは firmware/Fw_Update。
//
//   SDから   … StartFromSd(path)。ファイルアプリで .pfw を開く/「本体の更新」の[SDから選ぶ]
//   Wi-Fiで  … StartListening()。PCの script/firmware_tool.py push(pio run -t fwpush)がTCPで送ってくる。
//               「本体の更新」の画面で[受け取る]を押している間だけ待ち受ける
//
// どちらも 見出しを読む → 署名・版・大きさを確かめる → (要るなら本体で確認)→ 内蔵フラッシュのLittleFSへ置く →
// 読み直して確かめる → arduino-picoのOTAのローダーへの指示を書く → 再起動(電源メニューの再起動と同じ後片付け)。
// 書き換えはローダーが次の起動で行い、終わるとまた再起動する。起動したら Setup() が置き場を片付け、
// 「#N に更新しました」を通知する。
namespace FirmwareFunctions {
    constexpr uint16_t kPort = 8370;
    // 電池で動いているときは、これを下回ると書き込まない(書き換えの途中で電源が落ちないように)
    constexpr int kMinBatteryPercent = 30;
    // 1フレームに進めるバイト数
    constexpr size_t kStepBytes = 8192;
    // 準備ができてから再起動するまで(画面に出す・PCへ DONE を届ける)
    constexpr uint32_t kRebootDelayMs = 1500;
    // TCPで何も届かなくなってから諦めるまで
    constexpr uint32_t kNetIdleTimeoutMs = 20000;

    // 起動時(通知の Setup() の後)。前回の更新の結果を通知し、置き場を片付ける
    void Setup();
    // 画面(FirmwareScene)が開いている間、毎フレーム
    void Update();

    // SDの更新ファイルから始める。始められなければfalseと理由
    bool StartFromSd(const char* path, FixedString<PICO_STR_LL>& err);
    // PCからの受け取りを待ち受ける
    bool StartListening(FixedString<PICO_STR_LL>& err);
    void StopListening();
    bool Listening();
    // 待ち受けている場所("192.168.1.5:8370")
    void ListenAddress(char* out, size_t cap);

    // 今の更新(無ければnullptr。終わった後も Dismiss() するまで結果を見られる)
    FwUpdate* Job();
    void Confirm(bool yes);
    // やめる(置き場を片付ける)
    void Cancel();
    // 終わった(失敗した)更新の結果を捨てる
    void Dismiss();
    // 更新の途中か(Done/Failed は含まない)
    bool Busy();
    // 再起動を待っているか
    bool RebootPending();

    // 書き込んでよい電源の状態か(だめなら理由)
    bool PowerOk(FixedString<PICO_STR_LL>& err);
    // 署名を確かめられない更新ファイルも受け付けるか(/sys/debug.cfg の fw-allow-unsigned)
    bool AllowUnsigned();
}
