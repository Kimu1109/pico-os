// ファームの更新(apps/firmware/Fw_Image・Fw_Update、FIRMWARE_UPDATE.md)の取り決めを固定する。
// 置き場(内蔵フラッシュのLittleFS)と受け取り元(SD・TCP)はメモリの偽物。
//
//   ./firmware_test                     … C++の中で組み立てた更新ファイルで全部の経路
//   ./firmware_test <x.pfw> <公開鍵.hex> … script/firmware_tool.py が作ったものを読めること(firmware_tool_test.py が呼ぶ)
#include "apps/firmware/Fw_Image.hpp"
#include "apps/firmware/Fw_Update.hpp"
#include "apps/ssh/Ssh_Sha256.hpp"
#include "monocypher-ed25519.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// 偽物
// ---------------------------------------------------------------------------
struct MemStore : public FwStore {
    uint32_t fs_start = 0x1024F000u, fs_end = 0x103FF000u;
    uint32_t cap = 1500000;
    bool mount_ok = true;
    bool mounted = false;
    int mounts = 0;
    Bytes payload;
    bool has_payload = false;
    std::string note;
    bool command = false;
    bool writing = false, reading = false;
    size_t read_pos = 0;
    long fail_write_after = -1;    // この位置を越えて書くと失敗
    long flip_on_read = -1;        // 読み直しでこの位置のバイトを化けさせる

    bool mount() override { mounts++; mounted = mount_ok; return mount_ok; }
    void unmount() override { mounted = false; writing = reading = false; }
    uint32_t fsStart() const override { return fs_start; }
    uint32_t fsEnd() const override { return fs_end; }
    void clear() override { payload.clear(); has_payload = false; note.clear(); command = false; writing = reading = false; }
    uint32_t capacity() override { return cap; }
    bool openWrite() override { payload.clear(); has_payload = true; writing = true; return true; }
    bool write(const uint8_t* p, size_t n) override {
        if(!writing) return false;
        if(fail_write_after >= 0 && (long)(payload.size() + n) > fail_write_after) return false;
        payload.insert(payload.end(), p, p + n);
        return true;
    }
    bool closeWrite() override { bool ok = writing; writing = false; return ok; }
    bool openRead() override { if(!has_payload) return false; reading = true; read_pos = 0; return true; }
    int read(uint8_t* p, size_t n) override {
        if(!reading) return -1;
        size_t k = payload.size() - read_pos;
        if(k > n) k = n;
        memcpy(p, payload.data() + read_pos, k);
        if(flip_on_read >= (long)read_pos && flip_on_read < (long)(read_pos + k)) p[flip_on_read - read_pos] ^= 0x40;
        read_pos += k;
        return (int)k;
    }
    void closeRead() override { reading = false; }
    bool commit(const char* n) override { note = n; command = true; return true; }
    bool readNote(char* out, size_t cap_) override {
        if(note.empty()) return false;
        snprintf(out, cap_, "%s", note.c_str());
        return true;
    }
};

struct MemSource : public FwSource {
    Bytes data;
    size_t pos = 0;
    size_t chunk = 100000;
    size_t cut_at = (size_t)-1; // ここで切れる
    bool stall = false;         // 1回おきに「今は無い」
    bool toggle = false;
    bool is_closed = false;
    std::vector<std::string> replies;

    int read(uint8_t* p, size_t n) override {
        if(stall && (toggle = !toggle)) return 0;
        const size_t end = data.size() < cut_at ? data.size() : cut_at;
        if(pos >= end) return -1;
        size_t k = end - pos;
        if(k > n) k = n;
        if(k > chunk) k = chunk;
        memcpy(p, data.data() + pos, k);
        pos += k;
        return (int)k;
    }
    void reply(const char* line) override { replies.push_back(line); }
    bool closed() override { return is_closed; }
    bool replied(const char* prefix) const {
        for(const auto& r : replies) if(r.compare(0, strlen(prefix), prefix) == 0) return true;
        return false;
    }
};

// ---------------------------------------------------------------------------
// 更新ファイルを組み立てる(script/firmware_tool.py の make_header と同じ並び)
// ---------------------------------------------------------------------------
struct Spec {
    uint32_t flags = FwImage::kFlagSigned;
    const char* board = "rpipico2w";
    uint32_t serial = 120;
    const char* git = "abcdef1";
    uint32_t image_bytes = 0x40000;
    uint32_t isize = 0;           // gzipの末尾に書く大きさ(0ならimage_bytes)
    uint32_t flash_base = FwImage::kFlashBase;
    uint32_t fs_start = 0x1024F000u, fs_end = 0x103FF000u;
    size_t payload_len = 20000;
    bool gzip_magic = true;
    bool sign = true;             // 署名する(flagsとは別。印だけ立てて署名しない、を作れる)
    const uint8_t* seed = nullptr;// 署名の鍵(nullptrなら kSeed)
    uint8_t reserved_byte = 0;
    uint16_t header_bytes = 128;
};

static const uint8_t kSeed[32]  = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
static const uint8_t kSeed2[32] = {9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9};

static void Put32(uint8_t* p, uint32_t v){ p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void KeyPair(const uint8_t* seed_in, uint8_t sk[64], uint8_t pk[32]){
    uint8_t seed[32];
    memcpy(seed, seed_in, 32);
    crypto_ed25519_key_pair(sk, pk, seed);
}

static Bytes MakePayload(const Spec& s){
    Bytes p(s.payload_len);
    for(size_t i = 0; i < p.size(); i++) p[i] = (uint8_t)(i * 31 + 7);
    if(s.gzip_magic && p.size() >= 3){ p[0] = 0x1f; p[1] = 0x8b; p[2] = 0x08; }
    if(p.size() >= 4) Put32(&p[p.size() - 4], s.isize ? s.isize : s.image_bytes);
    return p;
}

static Bytes Build(const Spec& s, const Bytes* payload_override = nullptr){
    const Bytes payload = payload_override ? *payload_override : MakePayload(s);
    uint8_t h[128] = {};
    memcpy(h, "PICO-FW1", 8);
    h[8] = (uint8_t)s.header_bytes; h[9] = (uint8_t)(s.header_bytes >> 8);
    h[10] = 1; h[11] = 0;
    Put32(h + 12, s.flags);
    memcpy(h + 16, s.board, strlen(s.board));
    Put32(h + 32, s.serial);
    Put32(h + 36, 1760000000u);
    memcpy(h + 40, s.git, strlen(s.git));
    Put32(h + 56, s.image_bytes);
    Put32(h + 60, (uint32_t)payload.size());
    Put32(h + 64, s.flash_base);
    Put32(h + 68, s.fs_start);
    Put32(h + 72, s.fs_end);
    Sha256 sha;
    sha.update(payload.data(), payload.size());
    sha.finish(h + 76);
    h[110] = s.reserved_byte;

    uint8_t sig[64] = {};
    if(s.sign){
        uint8_t sk[64], pk[32];
        KeyPair(s.seed ? s.seed : kSeed, sk, pk);
        uint8_t msg[16 + 128];
        memcpy(msg, FwImage::kSignContext, 16);
        memcpy(msg + 16, h, 128);
        crypto_ed25519_sign(sig, sk, msg, sizeof(msg));
    }
    Bytes out(h, h + 128);
    out.insert(out.end(), sig, sig + 64);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// 信頼する鍵(kSeed)
static uint8_t g_keys[1][32];

static FwUpdate::Env MakeEnv(){
    FwUpdate::Env e;
    e.current_serial = 100;
    e.current_git = "1111111";
    e.keys = g_keys;
    e.key_count = 1;
    return e;
}

// 終わるまで回す(回数の上限つき)
static void Run(FwUpdate& u, size_t budget = 8192, int max_steps = 100000){
    for(int i = 0; i < max_steps && !u.finished() && u.state() != FwUpdate::State::Confirm; i++) u.step(budget);
}

struct Case {
    MemStore store;
    MemSource src;
    FwUpdate up;
    explicit Case(const Bytes& file){ src.data = file; }
    void start(const FwUpdate::Env& env){ up.start(&src, &store, env); }
};

static bool Contains(const char* s, const char* part){ return strstr(s, part) != nullptr; }

// ---------------------------------------------------------------------------
int main(int argc, char** argv){
    uint8_t sk[64];
    KeyPair(kSeed, sk, g_keys[0]);

    if(argc == 3){
        // ---- script/firmware_tool.py が作ったものを読む ----
        FILE* f = fopen(argv[1], "rb");
        if(!f){ printf("[FAIL] %s を開けない\n", argv[1]); return 1; }
        Bytes file;
        int c;
        while((c = fgetc(f)) != EOF) file.push_back((uint8_t)c);
        fclose(f);
        FILE* k = fopen(argv[2], "r");
        char hex[80] = {0};
        if(!k || !fgets(hex, sizeof(hex), k)){ printf("[FAIL] 鍵を読めない\n"); return 1; }
        fclose(k);
        uint8_t key[1][32];
        for(int i = 0; i < 32; i++){ unsigned v; sscanf(hex + i * 2, "%2x", &v); key[0][i] = (uint8_t)v; }

        FwImage::Header h;
        check(FwImage::Parse(file.data(), file.size(), h) == FwImage::Error::None, "python: 見出しを読める");
        check(strcmp(h.board, "rpipico2w") == 0 && h.serial == 4321 && strcmp(h.git, "beef123+") == 0, "python: 基板・版・git");
        check(h.fs_start == 0x1024F000u && h.fs_end == 0x103FF000u, "python: LittleFSの範囲を firmware.bin から読んでいる");
        check(FwImage::VerifySignature(file.data(), file.data() + 128, key, 1) == FwImage::Sig::Trusted, "python: Ed25519の署名をMonocypherで確かめられる");

        Case t(file);
        FwUpdate::Env env = MakeEnv();
        env.keys = key;
        t.store.cap = 2000000;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Done, "python: 最後まで通る(SHA-256・gzipの頭と末尾の大きさ)");
        check(t.store.note == "serial=4321 git=beef123+", "python: 控え");
        printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
        return failures ? 1 : 0;
    }

    // ---- 署名済み・新しい版: 確認なしで最後まで ----
    {
        Spec s;
        Case t(Build(s));
        t.src.chunk = 777;
        t.start(MakeEnv());
        Run(t.up, 3000);
        check(t.up.state() == FwUpdate::State::Done, "署名済み: Doneまで");
        check(t.up.signature() == FwImage::Sig::Trusted, "署名済み: Trusted");
        check(t.store.command && t.store.note == "serial=120 git=abcdef1", "署名済み: 指示と控えを書いた");
        check(t.store.payload == MakePayload(s), "署名済み: 置き場の中身が同じ");
        check(!t.store.mounted, "署名済み: 終わったら閉じている");
        check(t.src.replies.size() == 2 && t.src.replies[0] == "OK" && t.src.replies[1] == "DONE", "署名済み: OK → DONE");
        check(t.up.progressDone() == t.up.progressTotal(), "署名済み: 進み具合が全部");
        // 再起動の前なら取り消せる
        t.up.cancel("やめた");
        check(t.up.state() == FwUpdate::State::Failed && !t.store.command && !t.store.has_payload, "Doneの後の取り消しで指示も消える");
    }

    // ---- 少しずつ・途中で何度も「今は無い」 ----
    {
        Case t(Build(Spec{}));
        t.src.chunk = 1;
        t.src.stall = true;
        t.start(MakeEnv());
        Run(t.up, 64, 2000000);
        check(t.up.state() == FwUpdate::State::Done, "1バイトずつ・途切れながらでも通る");
    }

    // ---- 1回の step() は budget を大きく越えない ----
    {
        Case t(Build(Spec{}));
        t.start(MakeEnv());
        t.up.step(8192); // 見出し → Payload
        const uint32_t before = t.up.progressDone();
        t.up.step(1000);
        check(t.up.progressDone() - before <= 4096 + 1000, "step(budget): 1回で進むのは budget + バッファ1つまで");
    }

    // ---- SDから選んだとき(always_confirm)は確認する ----
    {
        Case t(Build(Spec{}));
        FwUpdate::Env env = MakeEnv();
        env.always_confirm = true;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Confirm, "always_confirm: 確認で止まる");
        check(t.up.confirmText()[0] == '\0', "always_confirm: 注意書きは無い");
        check(t.src.replied("WAIT"), "always_confirm: WAIT を返す");
        t.up.confirm(true);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Done, "確認して書き込む");
    }
    {
        Case t(Build(Spec{}));
        FwUpdate::Env env = MakeEnv();
        env.always_confirm = true;
        t.start(env);
        Run(t.up);
        t.up.confirm(false);
        check(t.up.state() == FwUpdate::State::Failed && !t.store.has_payload && !t.store.command, "やめる: 何も残さない");
        check(t.src.replied("ERR"), "やめる: ERR を返す");
    }
    {
        Case t(Build(Spec{}));
        FwUpdate::Env env = MakeEnv();
        env.always_confirm = true;
        t.start(env);
        Run(t.up);
        t.src.is_closed = true;
        t.up.step(100);
        check(t.up.state() == FwUpdate::State::Failed, "確認を待つ間に相手が切れたら失敗");
    }

    // ---- 署名なし ----
    {
        Spec s; s.flags = 0; s.sign = false;
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "署名の無い"), "署名なし: 既定では断る");
        check(t.store.mounts == 0, "署名なし: 置き場に触らない");
    }
    {
        Spec s; s.flags = 0; s.sign = false;
        Case t(Build(s));
        FwUpdate::Env env = MakeEnv();
        env.allow_unsigned = true;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Confirm && Contains(t.up.confirmText(), "署名がありません"), "署名なし: 許していれば確認");
        t.up.confirm(true);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Done, "署名なし: 確認して書ける");
    }
    // ---- 知らない鍵 ----
    {
        Spec s; s.seed = kSeed2;
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && t.up.signature() == FwImage::Sig::UnknownKey, "知らない鍵: 断る");
    }
    {
        Spec s; s.seed = kSeed2;
        Case t(Build(s));
        FwUpdate::Env env = MakeEnv();
        env.allow_unsigned = true;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Confirm && Contains(t.up.confirmText(), "知らない鍵"), "知らない鍵: 許していれば確認");
    }
    // ---- 壊れた署名 ----
    {
        Bytes f = Build(Spec{});
        f[128 + 5] ^= 1;
        Case t(f);
        FwUpdate::Env env = MakeEnv();
        env.allow_unsigned = true;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed ||
              (t.up.state() == FwUpdate::State::Confirm && t.up.signature() == FwImage::Sig::UnknownKey),
              "署名を化けさせると信頼しない(許していても確認になる)");
    }
    {
        Spec s; s.flags = FwImage::kFlagSigned; s.sign = false; // 印だけ立って署名が0
        Case t(Build(s));
        FwUpdate::Env env = MakeEnv();
        env.allow_unsigned = true;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && t.up.signature() == FwImage::Sig::Bad, "印があるのに署名が空: Bad");
    }
    {
        Spec s; s.flags = 0; // 印が無いのに署名が入っている
        Case t(Build(s));
        FwUpdate::Env env = MakeEnv();
        env.allow_unsigned = true;
        t.start(env);
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && t.up.signature() == FwImage::Sig::Bad, "印が無いのに署名がある: Bad");
    }
    {
        Bytes f = Build(Spec{});
        f[32] ^= 1; // 署名した後に版を書き換える
        Case t(f);
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && t.up.signature() != FwImage::Sig::Trusted, "署名の後に見出しを書き換えると信頼しない");
    }
    // ---- 鍵が全部0(未登録)なら信頼しない ----
    {
        uint8_t zero[1][32] = {};
        Bytes f = Build(Spec{});
        check(FwImage::VerifySignature(f.data(), f.data() + 128, zero, 1) == FwImage::Sig::UnknownKey, "全部0の鍵は飛ばす");
    }

    // ---- 版 ----
    {
        Spec s; s.serial = 90;
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Confirm && Contains(t.up.confirmText(), "古い") && t.up.isDowngrade(), "古い版: 確認");
    }
    {
        Spec s; s.serial = 100; s.git = "1111111";
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Confirm && Contains(t.up.confirmText(), "同じ"), "同じ版: 確認");
    }
    {
        Spec s; s.serial = 100; s.git = "2222222+";
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Done, "同じ通し番号でも別のコミット(手元の変更)なら確認なし");
    }

    // ---- 見出しの検査 ----
    auto expectFail = [&](const char* label, const Spec& s, const char* part, uint32_t cap = 1500000){
        Case t(Build(s));
        t.store.cap = cap;
        t.start(MakeEnv());
        Run(t.up);
        const bool ok = t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), part) && !t.store.command;
        if(!ok) printf("       message=%s\n", t.up.message());
        check(ok, label);
    };
    { Spec s; s.board = "rpipico2";            expectFail("別の基板", s, "別の基板"); }
    { Spec s; s.fs_start = 0x10250000u;        expectFail("LittleFSの範囲が違う", s, "USB"); }
    { Spec s; s.image_bytes = 0x1024F000u - 0x10000000u + 1; expectFail("イメージがLittleFSへはみ出す", s, "大きさ"); }
    { Spec s; s.image_bytes = 0x3000;          expectFail("イメージが小さすぎる", s, "大きさ"); }
    { Spec s; s.payload_len = 17;              expectFail("gzipとして短すぎる", s, "大きさ"); }
    { Spec s; s.payload_len = 5000;            expectFail("置き場に入らない", s, "入りません", 4999); }
    { Spec s; s.flash_base = 0x20000000u;      expectFail("flash_baseが違う", s, "形式"); }
    { Spec s; s.reserved_byte = 1;             expectFail("予約の欄が0でない", s, "形式"); }
    { Spec s; s.flags = FwImage::kFlagSigned | 2; expectFail("知らない印", s, "形式"); }
    { Spec s; s.header_bytes = 120;            expectFail("見出しの長さが違う", s, "形式"); }
    {
        Bytes f = Build(Spec{});
        f[0] = 'X';
        Case t(f);
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "更新ファイルではありません"), "目印が違う");
    }
    {
        Case t(Bytes{});
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "空"), "空のファイル");
    }
    {
        Bytes f = Build(Spec{});
        f.resize(100);
        Case t(f);
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "短すぎ"), "見出しの途中で終わる");
    }

    // ---- 中身 ----
    {
        Bytes f = Build(Spec{});
        f[f.size() - 100] ^= 1; // 署名の後で中身を化けさせる
        Case t(f);
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "合いません"), "中身が化けている: 見出しのSHA-256と合わない");
        check(!t.store.has_payload && !t.store.command, "中身が化けている: 置き場を片付ける");
        check(t.src.replied("OK") && t.src.replied("ERR"), "中身が化けている: OK の後に ERR");
    }
    {
        Bytes f = Build(Spec{});
        Case t(f);
        t.src.cut_at = f.size() - 10;
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "途中で切れ"), "途中で切れる");
        check(!t.store.has_payload, "途中で切れる: 置き場を片付ける");
    }
    {
        Case t(Build(Spec{}));
        t.store.fail_write_after = 5000;
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "書き込みに失敗"), "置き場へ書けない");
    }
    {
        Case t(Build(Spec{}));
        t.store.flip_on_read = 1234;
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "読み直した"), "読み直すと化けている");
        check(!t.store.command, "読み直すと化けている: 指示を書かない");
    }
    {
        Spec s; s.isize = 0x40001; // 末尾の大きさが見出しと違う(SHA-256は合っている)
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "大きさ"), "gzipの末尾の大きさが見出しと違う");
    }
    {
        Spec s; s.gzip_magic = false;
        Case t(Build(s));
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "gzip"), "gzipでない中身");
    }
    {
        Case t(Build(Spec{}));
        t.store.mount_ok = false;
        t.start(MakeEnv());
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Failed && Contains(t.up.message(), "置き場"), "置き場が開けない");
    }
    // ---- 前の更新の残りは消してから始める ----
    {
        Case t(Build(Spec{}));
        t.store.payload = Bytes(10, 0xAA);
        t.store.has_payload = true;
        t.store.note = "serial=1 git=old";
        t.store.command = true;
        t.start(MakeEnv());
        t.up.step(8192);
        check(t.store.note.empty() && !t.store.command, "前の残り(控え・指示)を消す");
        Run(t.up);
        check(t.up.state() == FwUpdate::State::Done && t.store.note == "serial=120 git=abcdef1", "前の残りがあっても通る");
    }
    // ---- 途中で取り消す ----
    {
        Case t(Build(Spec{}));
        t.src.chunk = 100;
        t.start(MakeEnv());
        for(int i = 0; i < 20; i++) t.up.step(200);
        check(t.up.state() == FwUpdate::State::Payload, "途中まで書いた");
        t.up.cancel();
        check(t.up.state() == FwUpdate::State::Failed && !t.store.has_payload && !t.store.writing, "途中で取り消すと片付ける");
    }

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
