#pragma once

#include <cstdint>
#include <cstddef>

#include "util/FixedString.hpp"
#include "consts.hpp"

// ============================================================================
// 保存済みのWi-Fiネットワーク(SSIDとパスワードの組)の一覧と、Wi-FiのON/OFF。
//
// 置き場所は /sys/wifi.cfg(Config_Functionsと同じ key=value 形式):
//   enabled = true|false
//   ssid1 = enc1:...    pass1 = enc1:...
//   ssid2 = ...         pass2 = ...
//   ip2 = 192.168.1.50/24   gw2 = 192.168.1.1   dns2 = 1.1.1.1,8.8.8.8   (任意。下の IpConfig)
// **番号の若い順が「最後に接続できた順」**(1番 = 直近で接続したネットワーク)。
// 接続できるたびにその組を1番へ繰り上げる(MarkConnected)ので、並び自体が
// 「直近に接続したものから試す」自動接続の順番になる。
//
// SSID/パスワードはWi-Fiの単一設定(network.cfg)と同じPICO_Secretで暗号化する。
// 用途文字列は番号ごとに分ける("wifi-ssid:1"等)。同じ用途で複数の暗号文を
// 1つのファイルへ並べると、XORで平文同士のXORが漏れるため(カレンダーのURLと同じ)。
//
// 状態はすべて固定長の静的領域(約1.2KB)で、確保はしない。描画にもWi-Fiにも
// 依存しないので、ホストテスト(wifi_profiles_test)でそのまま動かせる。
// ============================================================================
namespace WifiProfiles {

    constexpr int    kMaxProfiles      = 8;
    // IEEE 802.11のSSIDは32バイトまで。WPAのパスフレーズは63文字(16進のPSKなら64桁)まで。
    // 暗号化すると "enc1:" + 16進128桁 = 133文字で、Config_Functionsの値の上限(160)に収まる
    constexpr size_t kMaxSsidBytes     = 32;
    constexpr size_t kMaxPasswordBytes = 64;

    // ネットワークごとのIPの設定。既定(全部0)はDHCPでアドレスもDNSももらう。
    //   static_ip … アドレス・マスク・ゲートウェイを固定する(DHCPを使わない)
    //   dns[]     … 0以外ならDHCPのDNSの代わりに使う(固定IPでなくても指定できる)
    // アドレスは util/Net_Addr.hpp の形(上位バイトが先頭のオクテット)。秘密ではないので暗号化しない
    constexpr int kMaxDns = 2;
    struct IpConfig {
        bool     static_ip = false;
        uint32_t ip = 0, mask = 0, gateway = 0;
        uint32_t dns[kMaxDns] = {};
        bool isDefault() const { return !static_ip && dns[0] == 0 && dns[1] == 0; }
    };

    struct Profile {
        FixedString<PICO_STR_M> ssid;
        FixedString<PICO_STR_L> password; // 空 = パスワード無し(オープンなネットワーク)
        IpConfig ip;
    };

    // IpConfigとして正しいか(固定IPなら、使えるアドレス・連続したマスク・ゲートウェイが同じサブネット。
    // ゲートウェイ0(無し)は許す: 外へ出ない閉じたLAN用)。DNSは0か使えるアドレス
    bool ValidIpConfig(const IpConfig& c);

    // /sys/wifi.cfgを読む。ファイルが無ければ、旧形式(network.cfgの wifi-ssid / wifi-password。
    // 1つだけ持てた頃の設定)を1番として取り込み、wifi.cfgを作る(以後network.cfgの2キーは読まない)。
    // 戻り値: wifi.cfgか旧形式のどちらかを読めたらtrue
    bool Load();
    // 今の一覧とON/OFFをwifi.cfgへ丸ごと書く(一時ファイル→差し替え)
    bool Save();
    // テスト用: 一覧を空・ONに戻す(SDには触らない)
    void Reset();

    int  Count();
    // index 0 が直近で接続したもの。範囲外はnullptr
    const Profile* At(int index);
    // SSIDの完全一致で探す。無ければ-1
    int  Find(const char* ssid);

    enum class PutResult : uint8_t {
        Added,    // 末尾へ新しく足した
        Updated,  // 同じSSIDがあったのでパスワードを書き換えた(並びは変えない)
        Full,     // kMaxProfiles件埋まっている
        Invalid,  // SSIDが空/長すぎる、パスワードが長すぎる
    };
    // 追加または更新して保存する。out_indexには入った位置(Full/Invalidなら-1)
    PutResult Put(const char* ssid, const char* password, int* out_index = nullptr);
    // 削除して保存する
    bool Remove(int index);
    // 接続できたSSIDを先頭へ繰り上げる。並びが変わったときだけ保存する
    // (同じネットワークへ繋がり直すたびにSDへ書かないため)。一覧に無ければ何もしない
    void MarkConnected(const char* ssid);
    // index 番目のIPの設定を書き換えて保存する。範囲外/正しくない設定ならfalse
    bool SetIpConfig(int index, const IpConfig& c);

    bool IsEnabled();
    // ON/OFFを切り替えて保存する
    void SetEnabled(bool enabled);
}
