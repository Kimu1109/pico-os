#include "services/DeviceKey_Functions.hpp"
#include "system/Config_Functions.hpp"
#include "services/Lock_Functions.hpp"
#include "system/Log_Functions.hpp"
#include "ssh/Ssh_Util.hpp"
#include "storage/SD_Path.hpp"
#include "util/Device_Key.hpp"
#include "util/Secret_Aead.hpp"
#include "util/Secret_Cipher.hpp"
#include "monocypher.h"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if !defined(PICOOS_PC)
#include "pico/bootrom.h"
#endif

namespace {
    // ---- OTPの置き場所 ----
    // RP2350のOTPは64行=1ページ。ページ0〜2は工場の情報・起動の設定・Secure Bootの鍵、62〜63は
    // OTPの鍵とページのロックに使われる。3〜61が利用者向け。その中の60ページ目を使う
    constexpr uint16_t kPage = 60;
    constexpr uint16_t kBaseRow = kPage * 64;
    constexpr int kSlots = 2;
    constexpr uint16_t kSlotStride = 24;
    constexpr int kKeyBytes = 32;
    // 枠の中の行(1行=ECCつき16bit)
    constexpr int kRowMagic = 0;
    constexpr int kRowVersion = 1;
    constexpr int kRowKey = 2;                     // 16行
    constexpr int kRowCheck = kRowKey + kKeyBytes / 2;   // 2行
    constexpr int kSlotRows = kRowCheck + 2;       // 20行
    constexpr uint16_t kMagic = 0x4B44;            // "DK"
    constexpr uint16_t kVersion = 1;
    static_assert(kSlotRows <= kSlotStride, "枠が重なる");
    static_assert(kSlotStride * kSlots <= 64, "1ページに収まらない");

    DeviceKeyFunctions::State state = DeviceKeyFunctions::State::None;

    // ======================================================================
    // OTPの読み書き(実機はboot ROMの otp_access、PC/ホストテストは真似る)
    // ======================================================================
#if defined(PICOOS_PC)
    constexpr int kEmuRows = 64;
    uint32_t emu_raw[kEmuRows] = {};   // ページ60だけ。0=書いていない、それ以外=0x1000000|値(ECCの代わりの印)
    bool emu_loaded = false;
    int emu_fail_after = -1;

    void EmuLoad(){
        if(emu_loaded) return;
        emu_loaded = true;
        if(!DeviceKeyFunctions::pc_otp_file) return;
        FILE* f = fopen(DeviceKeyFunctions::pc_otp_file, "rb");
        if(!f) return;
        if(fread(emu_raw, sizeof(emu_raw), 1, f) != 1) memset(emu_raw, 0, sizeof(emu_raw));
        fclose(f);
    }

    void EmuSave(){
        if(!DeviceKeyFunctions::pc_otp_file) return;
        FILE* f = fopen(DeviceKeyFunctions::pc_otp_file, "wb");
        if(!f) return;
        fwrite(emu_raw, sizeof(emu_raw), 1, f);
        fclose(f);
    }

    bool InEmu(uint16_t row, int n){ return row >= kBaseRow && row + n <= kBaseRow + kEmuRows; }

    bool ReadRaw(uint16_t row, uint32_t* out, int n){
        EmuLoad();
        if(!InEmu(row, n)) return false;
        for(int i = 0; i < n; i++) out[i] = emu_raw[row - kBaseRow + i];
        return true;
    }

    bool ReadEcc(uint16_t row, uint16_t* out, int n){
        EmuLoad();
        if(!InEmu(row, n)) return false;
        for(int i = 0; i < n; i++){
            const uint32_t r = emu_raw[row - kBaseRow + i];
            //印の無い生の値(壊れた行)は、実機のECCの訂正できない誤りと同じく読めない
            if(r != 0 && (r & 0xFF000000u) != 0x01000000u) return false;
            out[i] = (uint16_t)r;
        }
        return true;
    }

    bool WriteEcc(uint16_t row, const uint16_t* data, int n){
        EmuLoad();
        if(!InEmu(row, n)) return false;
        for(int i = 0; i < n; i++){
            if(emu_fail_after == 0){ EmuSave(); return false; }
            if(emu_fail_after > 0) emu_fail_after--;
            uint32_t& r = emu_raw[row - kBaseRow + i];
            if(r != 0){ EmuSave(); return false; }   //書いた行は書き直せない
            r = 0x01000000u | data[i];
        }
        EmuSave();
        return true;
    }
#else
    // boot ROMの otp_access。flagsの下位16bitが行、ECCなら1行2バイト、生なら1行4バイト(24bit)
    bool ReadRaw(uint16_t row, uint32_t* out, int n){
        otp_cmd_t cmd;
        cmd.flags = row;
        return rom_func_otp_access((uint8_t*)out, (uint32_t)n * 4, cmd) == BOOTROM_OK;
    }

    bool ReadEcc(uint16_t row, uint16_t* out, int n){
        otp_cmd_t cmd;
        cmd.flags = row | OTP_CMD_ECC_BITS;
        return rom_func_otp_access((uint8_t*)out, (uint32_t)n * 2, cmd) == BOOTROM_OK;
    }

    bool WriteEcc(uint16_t row, const uint16_t* data, int n){
        //1行ずつ書く(どこで失敗したかを残すため。失敗した行より後は書かない)
        for(int i = 0; i < n; i++){
            uint16_t v = data[i];
            otp_cmd_t cmd;
            cmd.flags = (uint32_t)(row + i) | OTP_CMD_ECC_BITS | OTP_CMD_WRITE_BITS;
            if(rom_func_otp_access((uint8_t*)&v, 2, cmd) != BOOTROM_OK) return false;
        }
        return true;
    }
#endif

    uint16_t SlotRow(int slot){ return (uint16_t)(kBaseRow + slot * kSlotStride); }

    void CheckValue(const uint8_t key[kKeyBytes], uint16_t out[2]){
        static const uint8_t tag[] = "pico-os-otp-key-check";
        uint8_t h[4];
        crypto_blake2b_keyed(h, sizeof(h), key, kKeyBytes, tag, sizeof(tag));
        out[0] = (uint16_t)(h[0] | (h[1] << 8));
        out[1] = (uint16_t)(h[2] | (h[3] << 8));
    }

    enum class SlotState : uint8_t { Empty, Valid, Broken };

    // 枠を読む。Validなら key に鍵
    SlotState ReadSlot(int slot, uint8_t key[kKeyBytes]){
        uint32_t raw[kSlotRows];
        if(!ReadRaw(SlotRow(slot), raw, kSlotRows)) return SlotState::Broken;
        bool empty = true;
        for(int i = 0; i < kSlotRows; i++) if(raw[i] != 0) empty = false;
        if(empty) return SlotState::Empty;

        uint16_t rows[kSlotRows];
        if(!ReadEcc(SlotRow(slot), rows, kSlotRows)) return SlotState::Broken;
        if(rows[kRowMagic] != kMagic || rows[kRowVersion] != kVersion) return SlotState::Broken;
        for(int i = 0; i < kKeyBytes / 2; i++){
            key[i * 2] = (uint8_t)rows[kRowKey + i];
            key[i * 2 + 1] = (uint8_t)(rows[kRowKey + i] >> 8);
        }
        uint16_t check[2];
        CheckValue(key, check);
        const bool ok = check[0] == rows[kRowCheck] && check[1] == rows[kRowCheck + 1];
        crypto_wipe(rows, sizeof(rows));
        if(!ok){
            crypto_wipe(key, kKeyBytes);
            return SlotState::Broken;
        }
        return SlotState::Valid;
    }

    // 鍵→確かめの値→版→目印の順に書く(目印が最後なので、書きかけは目印の無い壊れた枠になる)
    bool WriteSlot(int slot, const uint8_t key[kKeyBytes]){
        uint16_t rows[kKeyBytes / 2 + 2];
        for(int i = 0; i < kKeyBytes / 2; i++) rows[i] = (uint16_t)(key[i * 2] | (key[i * 2 + 1] << 8));
        CheckValue(key, rows + kKeyBytes / 2);
        bool ok = WriteEcc(SlotRow(slot) + kRowKey, rows, kKeyBytes / 2 + 2);
        crypto_wipe(rows, sizeof(rows));
        const uint16_t version = kVersion, magic = kMagic;
        ok = ok && WriteEcc(SlotRow(slot) + kRowVersion, &version, 1);
        ok = ok && WriteEcc(SlotRow(slot) + kRowMagic, &magic, 1);
        return ok;
    }

    // 端末の鍵から用途ごとの鍵を作って入れる(端末の鍵そのものは持ち続けない)
    void Derive(const uint8_t master[kKeyBytes]){
        uint8_t c[16], aead[32], pin[32];
        static const char t1[] = "pico-os/enc3", t2[] = "pico-os/aead", t3[] = "pico-os/pin";
        crypto_blake2b_keyed(c, sizeof(c), master, kKeyBytes, (const uint8_t*)t1, sizeof(t1));
        crypto_blake2b_keyed(aead, sizeof(aead), master, kKeyBytes, (const uint8_t*)t2, sizeof(t2));
        crypto_blake2b_keyed(pin, sizeof(pin), master, kKeyBytes, (const uint8_t*)t3, sizeof(t3));
        uint32_t cipher[4];
        for(int i = 0; i < 4; i++){
            cipher[i] = (uint32_t)c[i * 4] | ((uint32_t)c[i * 4 + 1] << 8) |
                        ((uint32_t)c[i * 4 + 2] << 16) | ((uint32_t)c[i * 4 + 3] << 24);
        }
        DeviceKey::Install(cipher, aead, pin);
        crypto_wipe(c, sizeof(c));
        crypto_wipe(cipher, sizeof(cipher));
        crypto_wipe(aead, sizeof(aead));
        crypto_wipe(pin, sizeof(pin));
    }

    // ======================================================================
    // SDの暗号化された設定を書き直す
    // ======================================================================
    // キー → 用途文字列。暗号化する項目でなければ false
    typedef bool (*PurposeFn)(const char* key, char* out, size_t cap);

    bool NumberedPurpose(const char* key, const char* prefix, const char* purpose, char* out, size_t cap){
        const size_t n = strlen(prefix);
        if(strncmp(key, prefix, n) != 0 || key[n] == '\0') return false;
        for(const char* p = key + n; *p; p++) if(*p < '0' || *p > '9') return false;
        snprintf(out, cap, "%s:%s", purpose, key + n);
        return true;
    }

    bool WifiPurpose(const char* key, char* out, size_t cap){
        return NumberedPurpose(key, "ssid", "wifi-ssid", out, cap) ||
               NumberedPurpose(key, "pass", "wifi-password", out, cap);
    }

    bool NetworkPurpose(const char* key, char* out, size_t cap){
        if(strcmp(key, "wifi-ssid") != 0 && strcmp(key, "wifi-password") != 0) return false;
        snprintf(out, cap, "%s", key);
        return true;
    }

    bool ChatPurpose(const char* key, char* out, size_t cap){
        if(strcmp(key, "token") != 0) return false;
        snprintf(out, cap, "chat-token");
        return true;
    }

    bool TodoistPurpose(const char* key, char* out, size_t cap){
        if(strcmp(key, "token") != 0) return false;
        snprintf(out, cap, "todoist-token");   // Todoist_Client.cpp の kTokenPurpose
        return true;
    }

    bool CalendarPurpose(const char* key, char* out, size_t cap){
        snprintf(out, cap, "calendar-url:%s", key);   // Calendar_Sync.cpp の CalendarUrlPurpose()
        return true;
    }

    constexpr size_t kLineCap = 640;   // カレンダーのURL(暗号化して最大515文字)が入る

    // path の各行のうち fn が対象とする値で、平文/enc1: のものを今の鍵で暗号化し直す。
    // max_value はその設定を読む側が受け付ける値の長さ(これを超えるなら書き直さずそのまま)
    void RewriteFile(const char* path, PurposeFn fn, size_t max_value, DeviceKeyFunctions::MigrateResult& res){
        if(!OSData::SD.exists(path)) return;
        char* line = (char*)malloc(kLineCap * 3);
        if(!line){ res.failed++; return; }
        char* value = line + kLineCap;
        char* buf = value + kLineCap;   // 復号した平文/暗号文

        char tmp[PICO_PATH_LEN];
        snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        FsFile src = OSData::SD.open(path, O_RDONLY);
        FsFile dst = OSData::SD.open(tmp, O_WRONLY | O_CREAT | O_TRUNC);
        bool ok = src && dst;
        int changed = 0;
        while(ok){
            const int n = src.fgets(line, kLineCap);
            if(n <= 0) break;
            if(n >= (int)kLineCap - 1 && line[n - 1] != '\n'){ ok = false; break; }   //長すぎる行(壊さないよう諦める)

            char key[PICO_Config::kConfigMaxKeyLen];
            char purpose[96];
            bool rewritten = false;
            if(PICO_Config::ParseLine(line, key, sizeof(key), value, kLineCap) == PICO_Config::ConfigLineResult::Entry &&
               fn(key, purpose, sizeof(purpose)) && PICO_Secret::NeedsUpgrade(value)){
                char plain[PICO_Secret::kMaxPlainBytes + 1];
                char back[PICO_Secret::kMaxPlainBytes + 1];
                if(PICO_Secret::Decrypt(purpose, value, plain, sizeof(plain)) &&
                   PICO_Secret::Encrypt(purpose, plain, buf, kLineCap) && strlen(buf) < max_value &&
                   PICO_Secret::Decrypt(purpose, buf, back, sizeof(back)) && strcmp(plain, back) == 0){
                    const int m = snprintf(line, kLineCap, "%s=%s\n", key, buf);
                    if(m > 0 && m < (int)kLineCap){
                        rewritten = true;
                        changed++;
                    }
                }
                if(!rewritten) res.failed++;
                crypto_wipe(plain, sizeof(plain));
                crypto_wipe(back, sizeof(back));
            }
            ok = PICO_Config::Detail::WriteRawLine(dst, line);
        }
        if(src) src.close();
        if(dst) dst.close();

        if(ok && changed > 0){
            ok = OSData::SD.remove(path) && OSData::SD.rename(tmp, path);
            if(ok) res.upgraded += changed;
        }
        if(!ok){
            LOG_SYS_FAIL("端末の鍵: %s を書き直せませんでした", path);
            res.failed++;
        }
        OSData::SD.remove(tmp);
        crypto_wipe(line, kLineCap * 3);
        free(line);
    }
}

void DeviceKeyFunctions::Setup(){
    DeviceKey::Clear();
    state = State::None;
    bool broken = false;
    for(int s = 0; s < kSlots; s++){
        uint8_t key[kKeyBytes];
        const SlotState st = ReadSlot(s, key);
        if(st == SlotState::Valid){
            Derive(key);
            crypto_wipe(key, sizeof(key));
            state = State::Ready;
            LOG_SYS_OK("DeviceKey Setup: 端末の鍵を読みました(枠%d)", s + 1);
            return;
        }
        if(st == SlotState::Broken) broken = true;
        else break;   //空の枠より後ろは見ない(作るのは前から)
    }
    if(broken){
        //壊れた枠しか無く、空きも無い
        bool has_empty = false;
        for(int s = 0; s < kSlots; s++){
            uint8_t key[kKeyBytes];
            if(ReadSlot(s, key) == SlotState::Empty) has_empty = true;
        }
        if(!has_empty){
            state = State::Broken;
            LOG_SYS_FAIL("DeviceKey Setup: OTPの端末の鍵が壊れています");
            return;
        }
        LOG_SYS_WARN("DeviceKey Setup: 書きかけの枠があります(次の枠に作れます)");
    }
    LOG_SYS_MSG("DeviceKey Setup: 端末の鍵はまだありません(固定鍵で暗号化します)");
}

DeviceKeyFunctions::State DeviceKeyFunctions::GetState(){ return state; }

const char* DeviceKeyFunctions::StateText(){
    switch(state){
        case State::Ready: return "作成済み";
        case State::Broken: return "壊れています";
        case State::None:
        default: return "未作成";
    }
}

bool DeviceKeyFunctions::Provision(char* msg, size_t cap){
    if(state == State::Ready){ snprintf(msg, cap, "端末の鍵はもうあります"); return false; }
    int slot = -1;
    for(int s = 0; s < kSlots && slot < 0; s++){
        uint8_t key[kKeyBytes];
        if(ReadSlot(s, key) == SlotState::Empty) slot = s;
        crypto_wipe(key, sizeof(key));
    }
    if(slot < 0){
        state = State::Broken;
        snprintf(msg, cap, "OTPに空きがありません");
        return false;
    }

    uint8_t key[kKeyBytes];
    bool zero = true;
    for(int tries = 0; tries < 4 && zero; tries++){
        SecretAead::Random(key, sizeof(key));
        for(int i = 0; i < kKeyBytes; i++) if(key[i] != 0) zero = false;
    }
    if(zero){ snprintf(msg, cap, "乱数を作れませんでした"); return false; }

    const bool wrote = WriteSlot(slot, key);
    uint8_t back[kKeyBytes];
    const bool verified = wrote && ReadSlot(slot, back) == SlotState::Valid && memcmp(back, key, kKeyBytes) == 0;
    crypto_wipe(back, sizeof(back));
    if(!verified){
        crypto_wipe(key, sizeof(key));
        LOG_SYS_FAIL("DeviceKey: OTPへの書き込みに失敗しました(枠%d)", slot + 1);
        Setup();   //書きかけの枠を壊れた枠として数え直す
        snprintf(msg, cap, "OTPへ書けませんでした%s", state == State::Broken ? "(空きもありません)" : "(もう一度試せます)");
        return false;
    }
    Derive(key);
    crypto_wipe(key, sizeof(key));
    state = State::Ready;
    LOG_SYS_OK("DeviceKey: 端末の鍵を作りました(枠%d)", slot + 1);

    const MigrateResult r = MigrateSecrets();
    snprintf(msg, cap, "端末の鍵を作りました。%d項目を暗号化し直しました%s%s", r.upgraded,
             r.failed ? "(失敗あり。ログを見てください)" : "",
             r.pin_pending ? "。PINは次に解除したとき" : "");
    return true;
}

DeviceKeyFunctions::MigrateResult DeviceKeyFunctions::MigrateSecrets(){
    MigrateResult res;
    if(!DeviceKey::available || !OSData::SD_usable) return res;
    using namespace PICO_Path::FILE;
    const size_t cfg_max = PICO_Config::kConfigMaxValueLen;
    RewriteFile(CFG::SYS_WIFI_CFG, &WifiPurpose, cfg_max, res);
    RewriteFile(CFG::SYS_NETWORK_CFG, &NetworkPurpose, cfg_max, res);
    RewriteFile(CFG::SYS_CHAT_CFG, &ChatPurpose, cfg_max, res);
    RewriteFile(CFG::SYS_TODOIST_CFG, &TodoistPurpose, cfg_max, res);
    RewriteFile(CALENDAR_SOURCES, &CalendarPurpose, PICO_STR_512B, res);   // Calendar_Sync.cppの読み取りの値の大きさ

    //SSHの秘密鍵: 読めば LoadPrivateKey() が今の鍵で書き直す
    if(OSData::SD.exists(SSH_ID_ED25519)){
        uint8_t secret[64], pub[32];
        SshUtil::KeyRewrite rw = SshUtil::KeyRewrite::None;
        SshUtil::LoadPrivateKey(SSH_ID_ED25519, secret, pub, &rw);
        crypto_wipe(secret, sizeof(secret));
        if(rw == SshUtil::KeyRewrite::Encrypted) res.upgraded++;
        else if(rw == SshUtil::KeyRewrite::Failed) res.failed++;
    }

    res.pin_pending = LockFunctions::RecordNeedsDeviceKeyUpgrade();
    LOG_SYS_MSG("端末の鍵: %d項目を暗号化し直しました(失敗%d)", res.upgraded, res.failed);
    return res;
}

#if defined(PICOOS_PC)
void DeviceKeyFunctions::Test::ResetOtp(){
    memset(emu_raw, 0, sizeof(emu_raw));
    emu_loaded = true;
    emu_fail_after = -1;
    EmuSave();
}
void DeviceKeyFunctions::Test::PokeRaw(uint16_t row, uint32_t raw){
    EmuLoad();
    if(InEmu(row, 1)) emu_raw[row - kBaseRow] = raw;
    EmuSave();
}
void DeviceKeyFunctions::Test::FailWriteAfter(int rows){ emu_fail_after = rows; }
#else
void DeviceKeyFunctions::Test::ResetOtp(){}
void DeviceKeyFunctions::Test::PokeRaw(uint16_t, uint32_t){}
void DeviceKeyFunctions::Test::FailWriteAfter(int){}
#endif
uint16_t DeviceKeyFunctions::Test::BaseRow(){ return kBaseRow; }
