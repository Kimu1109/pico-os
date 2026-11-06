#pragma once
#include <cstdint>

// SDに置いた追加の漢字フォント(.pkf)から字を引く係。docs/design/performance.md「漢字の一部をSDへ」。
//
// フラッシュの日本語フォント(src/gui/fonts/JpFont_Base*.cpp)には常用漢字までしか入っていないので、
// 無かった字だけ U8g2GlyphIndex::find() がここへ聞きに来る。返すのは u8g2 の字のデータの位置(フラッシュの字と同じ形)で、
// 中身はRAMの控え(slots)に写したもの。**次の find() で別の字に上書きされうる**ので、受け取った側はすぐ読み終えること
// (GlyphIndexFont の drawChar()/updateFontMetric() は1回の呼び出しの中で読み終える)。
//
// ファイルの形は script/font/split_font.py の冒頭。見出しの印・字の高さ・大きさが決まりと合わないファイルは使わない(□になる)。
// 読むのは Reader 越し(実機・PCは SdFat の FsFile、ホストテストは作り物)。
//
// ---- SDが使えない間 ----
// Reader::generation() が0(SDが無い・USBドライブでPCに渡している等)の間はSDに触らず「無い」と答える。
// 番号が変わったら(マウントし直した)ファイルを開き直す。そのたびに epoch() が進むので、
// U8g2GlyphIndex は「無い」と覚えた控えを捨てる(SDが戻った・フォントを置いたら字が出る)。
//
// 2コア目からは使わない(描画は1コア目だけ)。new は使わず、控えの置き場は持ち主が渡す(固定長)。
class ExtGlyphStore {
public:
    static constexpr int kMaxIndex = 128;   // script/font/split_font.py の K_MAX_INDEX と同じ
    static constexpr int kHeaderSize = 28;

    class Reader {
    public:
        // 0 = SDを使えない。それ以外はマウントのたびに変わる番号。字を引くたびに呼ばれるので軽くすること
        virtual uint32_t generation() = 0;
        // path を読み用に開く(開いていたものは閉じる)。大きさを返す
        virtual bool open(const char* path, uint32_t& size) = 0;
        virtual bool readAt(uint32_t pos, void* dst, uint32_t len) = 0;
        virtual void close() = 0;
    protected:
        ~Reader() = default;
    };

    enum class State : uint8_t {
        NotOpened,   // まだ字を聞かれていない
        NoSd,        // SDを使えない
        Missing,     // ファイルが無い
        Bad,         // 形が違う・壊れている
        WrongFont,   // 別のファームの(印が違う)フォント
        ReadError,   // 読んでいる途中で失敗した(次にマウントし直すか reload() するまで使わない)
        Ready,
    };

    struct Expect {
        const char* path;
        uint8_t px;
        uint32_t id;
        uint32_t bytes;
    };

    // slots: slot_count × slot_bytes のバイト列。codes/flags: slot_count 個。slot_count は2の冪
    constexpr ExtGlyphStore(const Expect& expect, Reader* reader,
                            uint8_t* slots, uint16_t* codes, uint8_t* flags, int slot_count, int slot_bytes)
        : expect_(expect), reader_(reader), slots_(slots), codes_(codes), flags_(flags),
          slot_count_(slot_count), slot_bytes_(slot_bytes) {}

    // SDの番号を見て、変わっていれば開き直す。状態が変わるたびに増える番号を返す
    uint32_t poll();
    uint32_t epoch() const { return epoch_; }

    // code の字のデータ(u8g2 の記録の見出し3Bの後)。無い・使えないなら nullptr。poll() の後に呼ぶ
    const uint8_t* find(uint16_t code);

    // 開いているファイルをすぐ閉じ、次の poll() で開き直す(Webから落として置き換える前後)
    void reload();

    State state() const { return state_; }
    uint16_t glyphCount() const { return glyph_count_; }
    // 計測用: SDを読んだ回数(見出しと字の読みの合計)・控えに無かった回数
    uint32_t reads() const { return reads_; }
    uint32_t misses() const { return misses_; }

private:
    enum : uint8_t { kSlotEmpty = 0, kSlotHas = 1, kSlotNone = 2 };

    uint32_t changed(uint32_t gen);
    void openFile();
    void setState(State s);
    void clearSlots();
    const uint8_t* load(uint16_t code, int slot);
    bool read(uint32_t pos, void* dst, uint32_t len);

    Expect expect_;
    Reader* reader_;
    uint8_t* slots_;
    uint16_t* codes_;
    uint8_t* flags_;
    int slot_count_;
    int slot_bytes_;

    State state_ = State::NotOpened;
    uint32_t generation_ = 0xFFFFFFFFu;  // 最後に見たSDの番号(0xFFFFFFFF = まだ見ていない・開き直す)
    uint32_t epoch_ = 0;
    uint16_t glyph_count_ = 0;
    uint16_t index_count_ = 0;
    uint32_t data_pos_ = 0;
    uint32_t data_bytes_ = 0;
    uint16_t index_code_[kMaxIndex] = {};
    uint32_t index_pos_[kMaxIndex] = {};
    uint32_t reads_ = 0;
    uint32_t misses_ = 0;
};
