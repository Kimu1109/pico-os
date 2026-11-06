// SDの追加の漢字フォント(src/gui/core/ExtGlyphStore と U8g2GlyphIndex からの呼び出し)のテスト。
// docs/design/performance.md「漢字の一部をSDへ」。
//
// 確かめること:
//   - 実物の pc/sdcard/sys/fonts/jpgothic16/24.pkf を開け(印・大きさが src/gui/fonts/JpFont_Data.hpp と合う
//     = 作り直し忘れが無い)、ファイルの全部の字を引けて中身が同じ。ファイルに無い字は nullptr
//   - 控えの同じ場所を取り合う字を交互に引いても正しい。控えに当たればSDを読まない
//   - SDが使えない(generation()==0)間は読まず、戻ったら開き直す
//   - ファイルが無い・印が違う・大きさが違う・壊れている・読みに失敗したときの状態。reload() で開き直す
//   - U8g2GlyphIndex: フラッシュに無い漢字だけを追加のフォントへ聞く。SDが抜けて戻ったら、□と覚えた字も出る
#include "gui/core/ExtGlyphStore.hpp"
#include "gui/core/U8g2GlyphIndex.hpp"
#include "gui/fonts/JpFont_Data.hpp"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// メモリの上の「SD」
class MemReader final : public ExtGlyphStore::Reader {
public:
    uint32_t gen = 1;
    std::map<std::string, std::vector<uint8_t>> files;
    std::string opened;
    bool fail_reads = false;
    int reads = 0, opens = 0;

    uint32_t generation() override { return gen; }
    bool open(const char* path, uint32_t& size) override {
        opens++;
        auto it = files.find(path);
        opened.clear();
        if(it == files.end()) return false;
        opened = path;
        size = (uint32_t)it->second.size();
        return true;
    }
    bool readAt(uint32_t pos, void* dst, uint32_t len) override {
        reads++;
        if(fail_reads || opened.empty()) return false;
        const auto& f = files[opened];
        if((uint64_t)pos + len > f.size()) return false;
        memcpy(dst, f.data() + pos, len);
        return true;
    }
    void close() override { opened.clear(); }
};

static std::vector<uint8_t> ReadFile(const char* path){
    std::vector<uint8_t> v;
    FILE* fp = fopen(path, "rb");
    if(!fp) return v;
    uint8_t buf[4096];
    size_t n;
    while((n = fread(buf, 1, sizeof(buf), fp)) > 0) v.insert(v.end(), buf, buf + n);
    fclose(fp);
    return v;
}

static uint32_t Le32(const uint8_t* p){ return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

// .pkf の字の並びを素直に読む(テストの答え)。文字コード → 記録
static std::map<uint16_t, std::vector<uint8_t>> Records(const std::vector<uint8_t>& f){
    std::map<uint16_t, std::vector<uint8_t>> m;
    size_t p = Le32(&f[20]);
    for(;;){
        const uint16_t c = (uint16_t)((f[p] << 8) | f[p + 1]);
        if(c == 0) break;
        const uint8_t size = f[p + 2];
        m[c] = std::vector<uint8_t>(f.begin() + p, f.begin() + p + size);
        p += size;
    }
    return m;
}

// 作り物の .pkf(stride 字おきに索引)
static std::vector<uint8_t> MakePkf(uint8_t px, uint32_t id, const std::vector<uint16_t>& codes, int stride){
    std::vector<uint8_t> data;
    std::vector<std::pair<uint16_t, uint32_t>> index;
    uint16_t max_rec = 0;
    for(size_t i = 0; i < codes.size(); i++){
        if(i % stride == 0) index.push_back({codes[i], (uint32_t)data.size()});
        const uint8_t size = (uint8_t)(4 + i % 9);
        if(size > max_rec) max_rec = size;
        data.push_back((uint8_t)(codes[i] >> 8));
        data.push_back((uint8_t)codes[i]);
        data.push_back(size);
        for(int k = 3; k < size; k++) data.push_back((uint8_t)(codes[i] + k));
    }
    data.push_back(0); data.push_back(0);
    std::vector<uint8_t> f;
    auto le = [&](uint32_t v, int n){ for(int i = 0; i < n; i++) f.push_back((uint8_t)(v >> (8 * i))); };
    f.insert(f.end(), {'P', 'K', 'F', '1'});
    le(1, 1); le(px, 1); le((uint32_t)index.size(), 2);
    le(id, 4); le((uint32_t)codes.size(), 4);
    le(max_rec, 2); le(0, 2);
    le(28 + 6 * (uint32_t)index.size(), 4); le((uint32_t)data.size(), 4);
    for(auto& e : index){ le(e.first, 2); le(e.second, 4); }
    f.insert(f.end(), data.begin(), data.end());
    return f;
}

struct Slots {
    explicit Slots(int count, int bytes) : buf((size_t)count * bytes), codes(count), flags(count), count(count), bytes(bytes) {}
    std::vector<uint8_t> buf;
    std::vector<uint16_t> codes;
    std::vector<uint8_t> flags;
    int count, bytes;
};

static void TestRealFile(const JpFontData::Ext& e, const char* path){
    char label[160];
    MemReader r;
    r.files[e.sd_path] = ReadFile(path);
    const auto& file = r.files[e.sd_path];
    snprintf(label, sizeof label, "%dpx: %s を読める", e.px, path);
    check(!file.empty(), label);
    if(file.empty()) return;
    Slots s(32, e.max_record);
    ExtGlyphStore st({e.sd_path, e.px, e.id, e.bytes}, &r, s.buf.data(), s.codes.data(), s.flags.data(), s.count, s.bytes);
    st.poll();
    snprintf(label, sizeof label, "%dpx: 印・大きさが JpFont_Data.hpp と合う(split_font.py の作り直し忘れが無い)", e.px);
    check(st.state() == ExtGlyphStore::State::Ready && st.glyphCount() == e.glyphs, label);

    const auto recs = Records(file);
    snprintf(label, sizeof label, "%dpx: 字の数は %u", e.px, (unsigned)e.glyphs);
    check(recs.size() == e.glyphs, label);
    bool all = true;
    for(uint32_t c = 1; c <= 0xFFFF && all; c++){
        const uint8_t* g = st.find((uint16_t)c);
        auto it = recs.find((uint16_t)c);
        if(it == recs.end()){
            if(g) { printf("  U+%04X はファイルに無いのに返った\n", (unsigned)c); all = false; }
        }else if(!g || memcmp(g, it->second.data() + 3, it->second.size() - 3) != 0){
            printf("  U+%04X の中身が違う\n", (unsigned)c);
            all = false;
        }
    }
    snprintf(label, sizeof label, "%dpx: 0〜0xFFFFの全部で、ファイルの字は同じ中身・無い字は nullptr", e.px);
    check(all, label);
}

static void TestSmall(){
    std::vector<uint16_t> codes;
    for(int i = 0; i < 1000; i++) codes.push_back((uint16_t)(0x4E00 + i * 5 + (i % 3)));
    const uint32_t id = 0x12345678;
    MemReader r;
    r.files["/f.pkf"] = MakePkf(16, id, codes, 8);
    const uint32_t bytes = (uint32_t)r.files["/f.pkf"].size();
    Slots s(16, 16);
    ExtGlyphStore st({"/f.pkf", 16, id, bytes}, &r, s.buf.data(), s.codes.data(), s.flags.data(), s.count, s.bytes);

    check(st.state() == ExtGlyphStore::State::NotOpened && r.opens == 0, "聞かれるまで開かない");
    st.poll();
    check(st.state() == ExtGlyphStore::State::Ready, "開ける");

    // 控えの同じ場所を取り合う2字を交互に
    const uint16_t a = codes[0];
    uint16_t bb = a;  // 下の4ビット(控えの場所)が a と同じ字
    for(uint16_t c : codes) if(c != a && (c & 15) == (a & 15)){ bb = c; break; }
    bool ok = true;
    for(int i = 0; i < 6; i++){
        const uint8_t* ga = st.find(a);
        ok = ok && ga && ga[0] == (uint8_t)(a + 3);
        const uint8_t* gb = st.find(bb);
        ok = ok && gb && gb[0] == (uint8_t)(bb + 3);
    }
    check(ok && bb != a, "控えの同じ場所を取り合う字を交互に引いても正しい");

    const int before = r.reads;
    st.find(codes[500]);
    const int one = r.reads - before;
    st.find(codes[500]);
    check(r.reads - before == one && one > 0, "控えに当たればSDを読まない");
    check(one <= 8 + 1, "1字を読むのは区切りの中の見出し(8字ぶんまで)+字の1回");
    check(st.find(0x4E01) == nullptr && st.find(0x4DFF) == nullptr && st.find(0xFFFF) == nullptr, "無い字(間・前・後)");
    const int r2 = r.reads;
    st.find(0x4E01);
    check(r.reads == r2, "無いと分かった字も控えに覚える");

    // SDが使えなくなる → 戻る
    r.gen = 0;
    const int r3 = r.reads;
    st.poll();
    check(st.state() == ExtGlyphStore::State::NoSd && st.find(codes[1]) == nullptr, "SDが使えない間は「無い」");
    check(r.reads == r3, "SDが使えない間は読まない");
    r.gen = 2;
    st.poll();
    check(st.state() == ExtGlyphStore::State::Ready && st.find(codes[1]) != nullptr, "マウントし直したら開き直す");

    // 読みの失敗(抜いた)
    r.fail_reads = true;
    st.find(codes[900]);
    check(st.state() == ExtGlyphStore::State::ReadError, "読めなかったら ReadError");
    r.fail_reads = false;
    st.poll();
    check(st.find(codes[900]) == nullptr, "ReadError の間は使わない");
    r.gen = 3;
    st.poll();
    check(st.state() == ExtGlyphStore::State::Ready && st.find(codes[900]) != nullptr, "マウントし直せば戻る");

    // ファイルが無い → 置いて reload()
    MemReader r2b;
    ExtGlyphStore st2({"/f.pkf", 16, id, bytes}, &r2b, s.buf.data(), s.codes.data(), s.flags.data(), s.count, s.bytes);
    st2.poll();
    check(st2.state() == ExtGlyphStore::State::Missing, "ファイルが無ければ Missing");
    const int opens = r2b.opens;
    st2.poll();
    check(r2b.opens == opens, "無いファイルを毎回開きに行かない");
    r2b.files["/f.pkf"] = r.files["/f.pkf"];
    st2.reload();
    const uint32_t ep = st2.epoch();
    st2.poll();
    check(st2.state() == ExtGlyphStore::State::Ready && st2.epoch() != ep, "置いて reload() すれば使える(epoch が進む)");

    // 印違い・大きさ違い・壊れ
    struct Case { const char* label; std::vector<uint8_t> file; uint32_t id; uint32_t bytes; ExtGlyphStore::State want; };
    std::vector<uint8_t> trunc = r.files["/f.pkf"]; trunc.resize(trunc.size() - 10);
    std::vector<uint8_t> magic = r.files["/f.pkf"]; magic[0] = 'X';
    std::vector<uint8_t> px24 = MakePkf(24, id, codes, 8);
    std::vector<uint8_t> big = MakePkf(16, id, codes, 8); big[16] = 200;  // 1字の最大が控えより大きい
    std::vector<uint8_t> unordered = r.files["/f.pkf"]; unordered[28] = 0xFF; unordered[29] = 0xFF;  // 索引の最初が最大
    const Case cases[] = {
        {"印が違えば WrongFont", r.files["/f.pkf"], id + 1, bytes, ExtGlyphStore::State::WrongFont},
        {"字の高さが違えば WrongFont", px24, id, (uint32_t)px24.size(), ExtGlyphStore::State::WrongFont},
        {"途中で切れていれば Bad", trunc, id, bytes, ExtGlyphStore::State::Bad},
        {"頭の印が違えば Bad", magic, id, bytes, ExtGlyphStore::State::Bad},
        {"1字が控えより大きければ Bad", big, id, bytes, ExtGlyphStore::State::Bad},
        {"索引が順でなければ Bad", unordered, id, bytes, ExtGlyphStore::State::Bad},
    };
    for(const Case& c : cases){
        MemReader rc;
        rc.files["/f.pkf"] = c.file;
        ExtGlyphStore sc({"/f.pkf", 16, c.id, c.bytes}, &rc, s.buf.data(), s.codes.data(), s.flags.data(), s.count, s.bytes);
        sc.poll();
        check(sc.state() == c.want && sc.find(codes[0]) == nullptr, c.label);
    }
}

// U8g2GlyphIndex からの呼び出し
static void Push16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }

static std::vector<uint8_t> MakeU8g2(const std::vector<uint16_t>& codes){
    std::vector<uint8_t> f(23, 0);
    // 0〜255の字は 'A' だけ
    f[17] = 0; f[18] = 0; f[19] = 0; f[20] = 5;
    f.push_back('A'); f.push_back(3); f.push_back(0x11);
    f.push_back(0); f.push_back(0);
    const size_t uni = f.size();
    f[21] = (uint8_t)((uni - 23) >> 8); f[22] = (uint8_t)(uni - 23);
    Push16(f, 4); Push16(f, 0xFFFF);  // 表は1区画
    for(uint16_t c : codes){ Push16(f, c); f.push_back(4); f.push_back((uint8_t)c); }
    Push16(f, 0);
    return f;
}

static void TestIndexWithExt(){
    // フラッシュ: かな1字と漢字1字。SD: 別の漢字
    const std::vector<uint8_t> flash = MakeU8g2({0x3042, 0x4E00});
    const std::vector<uint16_t> ext_codes = {0x4E01, 0x9F8D};
    MemReader r;
    r.files["/e.pkf"] = MakePkf(16, 7, ext_codes, 1);
    Slots s(8, 16);
    ExtGlyphStore st({"/e.pkf", 16, 7, (uint32_t)r.files["/e.pkf"].size()}, &r, s.buf.data(), s.codes.data(), s.flags.data(), s.count, s.bytes);
    U8g2GlyphIndex idx(flash.data(), &st);

    check(idx.find(0x3042) == flash.data() + 23 + 5 + 4 + 3, "フラッシュの字はフラッシュから");
    const int r0 = r.reads;
    check(idx.find(0x3043) == nullptr && r.reads == r0, "漢字でない無い字はSDへ聞かない");
    const uint8_t* g = idx.find(0x4E01);
    check(g && g[0] == (uint8_t)(0x4E01 + 3), "フラッシュに無い漢字はSDから");
    const int r1 = r.reads;
    check(idx.find(0x4E01) == g && r.reads == r1, "2回目はSDを読まない");
    check(idx.find(0x4E02) == nullptr, "どちらにも無い漢字は nullptr");

    // SDが抜ける → □。戻ったら出る
    r.gen = 0;
    check(idx.find(0x9F8D) == nullptr, "SDが使えない間は□");
    r.gen = 5;
    const uint8_t* g2 = idx.find(0x9F8D);
    check(g2 && g2[0] == (uint8_t)(0x9F8D + 3), "SDが戻ったら、□と覚えた字も出る");
    check(idx.find(0x4E00) != nullptr && idx.find(0x4E01) != nullptr, "戻った後もフラッシュ・SDの両方を引ける");
}

int main(){
    TestRealFile(JpFontData::kExt16, "pc/sdcard/sys/fonts/jpgothic16.pkf");
    TestRealFile(JpFontData::kExt24, "pc/sdcard/sys/fonts/jpgothic24.pkf");
    TestSmall();
    TestIndexWithExt();
    if(failures){
        printf("%d 件失敗\n", failures);
        return 1;
    }
    printf("全部通りました\n");
    return 0;
}
