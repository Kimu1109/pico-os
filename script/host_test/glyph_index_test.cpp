// 日本語フォントの字の探し方(src/gui/core/U8g2GlyphIndex)のテスト。
//
// 作り物のu8g2形式のフォント(ヘッダ・0〜255の字・Unicodeの表・0x100以上の字)を組み立て、
// LovyanGFX の U8g2font::getGlyph() と同じ探し方(このファイルの Reference())と全部の文字コードで比べる。
//
// 確かめること:
//   - 0〜0xFFFF の全部で Reference() と同じ位置(無い字は nullptr)を返す。控えに入った2回目も同じ
//   - 索引は初めて0x100以上を引いたときに作られ、区切りは kIndexSlots 個以下、1区切りは ceil(字の数/kIndexSlots)
//   - 最初の字より前・字と字の間・最後の字より後の「無い字」
//   - 控えの同じ場所を取り合う字(文字コードの下の桁が同じ)を交互に引いても正しい
//   - 字の並びが文字コード順でないフォントは索引を使わず、元と同じ探し方になる
// 実物のフォント(lgfxJapanGothicP_16/24)での一致(幅の情報と描いた絵)はPCビルドで確かめた
// (docs/design/performance.md「日本語フォントの字の探し方」)。
#include "gui/core/U8g2GlyphIndex.hpp"

#include <cstdio>
#include <cstdint>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

static uint16_t Read16(const uint8_t* p){ return (uint16_t)((p[0] << 8) | p[1]); }

// LovyanGFX 1.2.28 の U8g2font::getGlyph() と同じ
static const uint8_t* Reference(const uint8_t* f, uint16_t encoding){
    const uint8_t* font = f + 23;
    if(encoding <= 255){
        if(encoding >= 'a')      font += Read16(f + 19);
        else if(encoding >= 'A') font += Read16(f + 17);
        for(; font[1]; font += font[1]){
            if(font[0] == encoding) return font + 2;
        }
        return nullptr;
    }
    font += Read16(f + 21);
    const uint8_t* lut = font;
    uint16_t e;
    do {
        font += Read16(lut);
        e = Read16(lut + 2);
        lut += 4;
    } while(e < encoding);
    for(; 0 != (e = Read16(font)); font += font[2]){
        if(e == encoding) return font + 3;
    }
    return nullptr;
}

static void Put16(std::vector<uint8_t>& v, size_t at, uint16_t x){ v[at] = (uint8_t)(x >> 8); v[at + 1] = (uint8_t)x; }
static void Push16(std::vector<uint8_t>& v, uint16_t x){ v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)x); }

// codes: 0x100以上の字の文字コード(この順に並べる)。block: 表の1区画の字の数
static std::vector<uint8_t> MakeFont(const std::vector<uint16_t>& codes, size_t block){
    std::vector<uint8_t> f(23, 0);
    // 0〜255の字: '!' 'A' 'B' 'Z' 'a' 'q' 'z' 0xE9。大きさは字ごとに変える(3〜5B)
    const uint8_t ascii[] = {'!', 'A', 'B', 'Z', 'a', 'q', 'z', 0xE9};
    for(uint8_t c : ascii){
        const size_t at = f.size();
        if(c == 'A') Put16(f, 17, (uint16_t)(at - 23));
        if(c == 'a') Put16(f, 19, (uint16_t)(at - 23));
        const uint8_t size = (uint8_t)(3 + c % 3);
        f.push_back(c);
        f.push_back(size);
        for(int i = 2; i < size; i++) f.push_back((uint8_t)(c ^ i));
    }
    f.push_back(0); f.push_back(0);
    // 0x100以上の部: 表 → 字
    const size_t uni = f.size();
    Put16(f, 21, (uint16_t)(uni - 23));
    const size_t blocks = (codes.size() + block - 1) / block;
    const size_t lut = f.size();
    f.resize(f.size() + 4 * (blocks ? blocks : 1));
    std::vector<size_t> block_start;
    for(size_t i = 0; i < codes.size(); i++){
        if(i % block == 0) block_start.push_back(f.size());
        const uint8_t size = (uint8_t)(4 + i % 4);
        Push16(f, codes[i]);
        f.push_back(size);
        for(int k = 3; k < size; k++) f.push_back((uint8_t)(i + k));
    }
    Push16(f, 0);
    f.push_back(0);
    if(blocks == 0){
        Put16(f, lut, (uint16_t)(f.size() - 3 - lut));
        Put16(f, lut + 2, 0xFFFF);
    }
    for(size_t b = 0; b < blocks; b++){
        const size_t from = (b == 0) ? lut : block_start[b - 1];
        Put16(f, lut + 4 * b, (uint16_t)(block_start[b] - from));
        const size_t last = std::min(codes.size(), (b + 1) * block) - 1;
        Put16(f, lut + 4 * b + 2, (b + 1 == blocks) ? 0xFFFF : codes[last]);
    }
    return f;
}

static bool MatchesAll(U8g2GlyphIndex& idx, const uint8_t* f){
    for(uint32_t c = 0; c <= 0xFFFF; c++){
        if(idx.find((uint16_t)c) != Reference(f, (uint16_t)c)){
            printf("  U+%04X が食い違う\n", (unsigned)c);
            return false;
        }
    }
    return true;
}

int main(){
    // ---- 文字コード順のフォント(4237字。日本語24pxと同じ数) ----
    std::vector<uint16_t> codes;
    for(int i = 0; i < 4237; i++) codes.push_back((uint16_t)(0x2000 + i * 3 + (i % 7 == 0 ? 1 : 0)));
    // 隣り合わない飛び(区画の境目の前後にも「無い字」がある)
    const std::vector<uint8_t> font = MakeFont(codes, 100);
    U8g2GlyphIndex idx(font.data());

    check(!idx.indexBuilt(), "索引は引くまで作らない");
    check(idx.find('A') == Reference(font.data(), 'A') && idx.find('A') != nullptr, "ASCIIの字を引ける");
    check(!idx.indexBuilt(), "0〜255だけなら索引を作らない");
    check(MatchesAll(idx, font.data()), "全部の文字コードで元の探し方と同じ(1回目)");
    check(idx.indexBuilt() && idx.sorted(), "索引ができ、文字コード順と分かる");
    check(idx.indexStride() == (4237 + U8g2GlyphIndex::kIndexSlots - 1) / U8g2GlyphIndex::kIndexSlots,
          "1区切りの字の数は ceil(字の数/区切りの数)");
    check(idx.indexCount() > 0 && idx.indexCount() <= U8g2GlyphIndex::kIndexSlots, "区切りの数は上限以下");
    check(MatchesAll(idx, font.data()), "控えに入った後も同じ(2回目)");
    idx.clearCache();
    check(MatchesAll(idx, font.data()), "控えを捨てても同じ");

    check(idx.find(0x1FFF) == nullptr, "最初の字より前は無い");
    check(idx.find(0x2002) == nullptr, "字と字の間は無い");
    check(idx.find(codes.back() + 1) == nullptr, "最後の字より後は無い");
    check(idx.find(codes.back()) != nullptr && idx.find(codes.front()) != nullptr, "最初と最後の字を引ける");

    // 控えの同じ場所(下の6ビットが同じ)を取り合う2字を交互に
    {
        const uint16_t a = codes[10], b = (uint16_t)(a + U8g2GlyphIndex::kCacheSlots * 3);
        bool ok = true;
        for(int i = 0; i < 10; i++){
            ok = ok && idx.find(a) == Reference(font.data(), a);
            ok = ok && idx.find(b) == Reference(font.data(), b);
        }
        check(ok, "控えの同じ場所を取り合う字を交互に引いても正しい");
    }

    // ---- 字の数が区切りの数より少ないフォント ----
    {
        std::vector<uint16_t> few = {0x3042, 0x3044, 0x3046, 0x4E00};
        const std::vector<uint8_t> f2 = MakeFont(few, 2);
        U8g2GlyphIndex i2(f2.data());
        check(MatchesAll(i2, f2.data()), "字が少ないフォントでも元と同じ");
        check(i2.indexStride() == 1 && i2.indexCount() == 4, "字が少なければ1字ずつ区切る");
    }

    // ---- 文字コード順でないフォント(作り物。u8g2の作り方からは起きない) ----
    {
        std::vector<uint16_t> bad = codes;
        std::swap(bad[500], bad[501]);
        const std::vector<uint8_t> f3 = MakeFont(bad, 100);
        U8g2GlyphIndex i3(f3.data());
        check(MatchesAll(i3, f3.data()), "順でないフォントでも元と同じ");
        check(i3.indexBuilt() && !i3.sorted(), "順でないと分かり索引を使わない");
    }

    if(failures){
        printf("%d 件失敗\n", failures);
        return 1;
    }
    printf("全部成功\n");
    return 0;
}
