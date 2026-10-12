#pragma once

#include <cstddef>
#include <cstdint>

// 端末ごとの鍵(RP2350のOTPに焼く32バイト)。
//
// - 鍵は本体の中で作る(ハードウェア乱数)。OTPから外へは出さず、用途ごとの鍵を作って
//   util/Device_Key.hpp へ入れる(Wi-Fi等の enc3:・Luaの暗号・画面ロックのPIN)
// - OTPのページ60(行0xF00〜)に2枠(各20行。ECCつき16bit/行): 目印・版・鍵16行・確かめの値2行。
//   鍵→確かめの値→版→目印の順に書くので、途中で電源が切れた枠は「目印が無いのに中身がある」=壊れた枠になる。
//   1枠目が壊れていれば2枠目に作れる
// - **OTPは一度書いたら消せない**。この鍵が無くなると(基板が壊れた等)、端末の鍵で暗号化したSDの
//   中身(Wi-Fiのパスワード・SSHの鍵・トークン等)は二度と読めない。元の値はPCに控えておくこと
// - Secure Bootは使っていないので、本体ごと持ち去られて別のファームを書かれたらOTPは読める。
//   守れるのは「SDカードだけを盗まれた/見られた」場合(リポジトリの固定鍵では読めなくなる)
// - PC/Webビルドは OTP をファイル(pc_otp_file)で真似る。ホストテストはメモリの中だけ
namespace DeviceKeyFunctions {

    enum class State : uint8_t {
        None,      // まだ作っていない(固定鍵のまま)
        Ready,     // 読めた。暗号化は端末の鍵で行う
        Broken,    // 枠が両方とも壊れている/読めない(作り直せない)
    };

    struct MigrateResult {
        int upgraded = 0;   // 端末の鍵で暗号化し直した値(ファイルの中の1項目ずつ)
        int failed = 0;     // 書き直せなかったファイル/値
        bool pin_pending = false;   // 画面ロックのPINは次に正しく打ったときに書き直す
    };

    // OTPを読み、鍵があれば Device_Key.hpp へ入れる。main.cppでSD・ログの後、
    // 暗号化された設定を読むもの(Network/Lock等)より前に1回
    void Setup();

    State GetState();
    const char* StateText();

    // 鍵を作ってOTPへ焼き、SDの暗号化された設定を端末の鍵で暗号化し直す。
    // 失敗したら false と理由(msg)。成功なら msg に結果の文
    bool Provision(char* msg, size_t cap);

    // SDの暗号化された設定(wifi.cfg・network.cfg・chat.cfg・todoist.cfg・カレンダーのURL・SSHの鍵)のうち、
    // 平文/固定鍵(enc1:)のものを端末の鍵で暗号化し直す。何度呼んでもよい
    MigrateResult MigrateSecrets();

    // PC/Webビルドで OTP を真似るファイル(main_pc.cppが決める。nullならメモリの中だけ)
    inline const char* pc_otp_file = nullptr;

    // ---- テスト用 ----
    namespace Test {
        // 真似たOTPを全部0へ戻す(PICOOS_PCのときだけ)
        void ResetOtp();
        // 真似たOTPの1行を生の値で書く(壊れた枠を作る)
        void PokeRaw(uint16_t row, uint32_t raw);
        // 次の書き込みを n 行目で失敗させる(-1で解除。書きかけの枠を作る)
        void FailWriteAfter(int rows);
        uint16_t BaseRow();
    }
}
