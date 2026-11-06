#pragma once
#include <cstdint>

class ExtGlyphStore;

// u8g2形式のフォント(LovyanGFXの lgfxJapanGothicP_* 等)で「文字コード → 字のデータの位置」を速く引く。
// docs/design/performance.md「日本語フォントの字の探し方」。
//
// LovyanGFX の U8g2font::getGlyph() は、Unicodeの表を先頭からたどってから区画の中の字を1つずつ見る。
// 日本語24px(4237字)で1文字あたり平均 表21.5項目 + 字の見出し50.9個 ≒ 72回フラッシュを読み、
// しかも幅を測る(updateFontMetric)と描く(drawChar)で1文字2回引く。フォントは277KBあって
// XIPのキャッシュ(16KB)に乗らないので、実機ではこの読みのほとんどがキャッシュの外れになる。
// 表に無い字(豆腐)は字の並びの終わりまで全部読んでいた。
//
// ここでは2段で引く:
//   1. 最近引いた字の控え(kCacheSlots個、文字コードで直接写像)。同じ字が続く本文はほぼここで済む
//   2. 字の並びを kIndexSlots 個おきに区切った索引(RAM)を二分探索し、区切りの中だけを順に見る
//      (24pxなら区切りは34字。並びは文字コード順なので、探す字より大きい字が来たら「無い」と分かる)
// 索引は初めて0x100以上の字を引いたときに1回だけ作る(全部の字の見出しを1回たどる)。
// 字の並びが文字コード順でなかったら(u8g2の作り方からは起きないはず)、索引を使わず元と同じ探し方をする。
//
// find() の返す値は getGlyph() と同じ(字の見出しを飛ばした所。無ければnullptr)。
//
// 追加のフォント(ExtGlyphStore、SDの .pkf)を渡してあれば、フォントに無かった漢字をそちらへ聞く
// (docs/design/performance.md「漢字の一部をSDへ」)。そのときの返す値はSDの字を写したRAMの位置で、
// 次の find() で上書きされうる(すぐ読み終えること)。控えには「SDにある」とだけ覚え、中身は ExtGlyphStore が持つ。
// 追加のフォントの状態が変わったら(SDを抜いた・置いた)控えを全部捨てる。
// 2コア目からは使わない(描画は1コア目だけ)。
class U8g2GlyphIndex {
public:
    static constexpr int kIndexSlots = 128;  // 索引の区切りの数(1区切り6B)
    static constexpr int kCacheSlots = 64;   // 控えの数(1個6B)

    // constexpr: グローバルのフォントを定数で初期化する(静的な初期化の順番に依らない)
    explicit constexpr U8g2GlyphIndex(const uint8_t* font, ExtGlyphStore* ext = nullptr) : font_(font), ext_(ext) {}

    const uint8_t* find(uint16_t encoding);

    // ---- テスト・計測用 ----
    // 控えを捨てる(索引は残す)
    void clearCache();
    // 索引を作り終えたか / 区切りの数 / 1区切りの字の数 / 字の並びが文字コード順だったか
    bool indexBuilt() const { return index_state_ != IndexState::NotBuilt; }
    int indexCount() const { return index_count_; }
    int indexStride() const { return index_stride_; }
    bool sorted() const { return index_state_ == IndexState::Ready; }

private:
    enum class IndexState : uint8_t { NotBuilt, Ready, Unsorted };

    // 控えの「位置」に入れる印。本物の位置はヘッダ(23B)より後なので0と1は使われない
    static constexpr uint32_t kEmpty = 0;
    static constexpr uint32_t kNotFound = 1;
    static constexpr uint32_t kInExt = 2;   // 追加のフォント(ext_)にある

    // 追加のフォントを聞いてよい字(漢字)。script/codegen/font/split_font.py の is_kanji() と同じ範囲
    static constexpr bool MayBeInExt(uint16_t e) { return (e >= 0x3400 && e <= 0x9FFF) || (e >= 0xF900 && e <= 0xFAFF); }

    const uint8_t* findAscii(uint16_t encoding) const;   // 0〜255(元と同じ探し方)
    const uint8_t* findUnicode(uint16_t encoding);       // 0x100以上
    const uint8_t* findUnicodeLinear(uint16_t encoding) const; // 元と同じ探し方(索引を使えないとき)
    const uint8_t* unicodeGlyphs() const;                // 0x100以上の字の並びの先頭
    void buildIndex();

    const uint8_t* font_;
    ExtGlyphStore* ext_;
    uint32_t ext_epoch_ = 0;

    uint16_t cache_code_[kCacheSlots] = {};
    uint32_t cache_pos_[kCacheSlots] = {};   // font_ からの位置(kEmpty/kNotFound は印)

    uint16_t index_code_[kIndexSlots] = {};  // 区切りの最初の字の文字コード
    uint32_t index_pos_[kIndexSlots] = {};   // その字の見出しの位置(font_ から)
    uint16_t index_count_ = 0;
    uint16_t index_stride_ = 0;
    IndexState index_state_ = IndexState::NotBuilt;
};
