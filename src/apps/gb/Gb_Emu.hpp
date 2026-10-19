#pragma once

#include <cstddef>
#include <cstdint>

#include "util/FixedString.hpp"
#include "apps/gb/Gb_Audio_Sink.hpp"
#include "consts.hpp"

// Game Boy(DMG)エミュの本体。lib/peanut_gb(Peanut-GB)を包むだけの薄い層。
//
// peanut_gb.h の実装を取り込むのは Gb_Emu.cpp の1箇所だけで、
// 他のファイル(シーン/ウィジェット)はこのクラスだけを見る。
//
// ---- ROMの置き場 ----
// Peanut-GBはROMを1バイトずつコールバックで読むので、SDから都度読むことはできない。2通りある:
//   - load(path): kMaxRomBytes(256KiB)までのROMをRAMへ丸ごと載せる。
//     テトリス/Dr.マリオ(32KiB)・マリオランド(64KiB)・カービィ(256KiB)が入る。それより大きいものは TooLarge
//   - load(path, image, size): 呼ぶ側が用意したROM(内蔵フラッシュへ書いたもの。apps/gb/Gb_Rom_Flash)を
//     そのまま読む。ROMにRAMを使わない。image は unload() まで有効でなければならない(解放はしない)
//
// ---- メモリ ----
// load()で確保し、unload()(とデストラクタ)で全部返す。シーンが開いている間だけの寿命。
//   - エミュの状態(WRAM/VRAM等)+画面   約23KB
//   - ROM                               ファイルの大きさぶん(最大256KiB)
//   - カートリッジRAM(セーブ)          0〜32KiB
// どれもmallocで1回ずつ確保する(ROMの大きさは開くまで分からないので固定長にできない)。
// 確保できなければ OutOfMemory で断るだけで落ちない。
//
// ---- 画面 ----
// 160x144を1画素2bit(白/薄灰/濃灰/黒の4段階)で詰めて持つ(1行40バイト、全体5760バイト)。
// 1行の中は左の画素ほど上位ビット。描画のたびに前回の内容と比べ、
// 変わった行の範囲だけを takeChangedRows() で渡す(静止画面なら液晶へ何も送らずに済む)。
//
// ---- 音 ----
// 音源チップ(0xFF10〜0xFF3F)への書き込みは、フレームの頭からのクロック数を付けて GbAudioSink へ渡す
// (setAudioSink()。実物は SoundFunctions::GbAudio() で、2コア目の GbApu が時刻どおりに鳴らす)。
// ゲームが読むときは、書かれた値の控えに「読むと常に1のビット」を足して答える(音源は2コア目にあって
// その場では聞けないため)。NR52の「鳴っているチャンネル」だけは音源が知らせた値(約1フレーム遅れ)と、
// このフレームにトリガーしたチャンネルを合わせて答える。
class GbEmu {
    public:
        constexpr static int kWidth = 160;
        constexpr static int kHeight = 144;
        constexpr static int kBytesPerRow = kWidth / 4; // 2bit/画素

        constexpr static uint32_t kMaxRomBytes = 256u * 1024u;
        constexpr static uint32_t kMaxCartRamBytes = 32u * 1024u;

        // 1フレームの長さ(70224クロック / 4.194304MHz = 約16.74ms)。マイクロ秒
        constexpr static uint32_t kFrameUs = 16743;

        // 押しているボタン(1 = 押している)。Peanut-GBのJOYPAD_*と同じ並び
        enum Button : uint8_t {
            A      = 0x01,
            B      = 0x02,
            Select = 0x04,
            Start  = 0x08,
            Right  = 0x10,
            Left   = 0x20,
            Up     = 0x40,
            Down   = 0x80,
        };

        enum class LoadError : uint8_t {
            None,
            NoSd,             // SDカードが使えない
            NotFound,         // ファイルを開けない
            TooLarge,         // kMaxRomBytes を超えている(第1段では載せられない)
            TooSmall,         // ヘッダ(0x150バイト)すら無い
            OutOfMemory,      // 確保に失敗した
            ReadFailed,       // 読み込みの途中で失敗した
            InvalidChecksum,  // ヘッダのチェックサムが合わない(ROMではない/壊れている)
            Unsupported,      // 対応していないカートリッジ(MBC)
            SaveTooLarge,     // カートリッジRAMが kMaxCartRamBytes を超える
        };

        GbEmu() = default;
        ~GbEmu(){ this->unload(); }
        GbEmu(const GbEmu&) = delete;
        GbEmu& operator=(const GbEmu&) = delete;

        // ROMをSDから読んで起動する。既に読み込み済みなら先に unload() する。
        // 同じ場所に「拡張子を.savにしたファイル」があれば、それをカートリッジRAMへ読む
        LoadError load(const char* rom_path);
        // 用意済みのROM(image, size)で起動する。rom_path は .sav・途中保存の置き場を決めるためだけに使う
        LoadError load(const char* rom_path, const uint8_t* image, uint32_t size);
        // ROMを外の置き場(内蔵フラッシュ)から読んでいるか
        bool romIsExternal() const { return this->rom && !this->rom_owned; }

        // 未保存のセーブがあれば書き出してから全部解放する
        void unload();

        bool loaded() const { return this->impl != nullptr; }

        // 1フレームぶん進める。エミュが止まっている(未読み込み/エラー)なら何もしない
        void runFrame();

        // 音の渡し先。load()より前に設定する(nullptrなら音を出さない。書き込みの控えだけ持つ)
        void setAudioSink(GbAudioSink* sink){ this->audio_sink = sink; }

        // 押しているボタン(Button のOR)を渡す
        void setButtons(uint8_t pressed);

        // カートリッジRAMに書き込みがあれば .sav へ書き出す。書いたらtrue
        bool writeSave();
        bool hasUnsavedData() const { return this->cart_ram_dirty; }

        // 実行中に回復できない誤り(不正な命令等)が起きて止まったか
        bool crashed() const { return this->crashed_; }
        const char* crashMessage() const { return this->crash_message.c_str(); }

        // ROMのヘッダに書かれた題名(空のこともある)
        const char* title() const { return this->title_.c_str(); }
        uint32_t romSize() const { return this->rom_size; }

        // 画面。rowは0〜143、1行 kBytesPerRow バイト。未読み込みならnullptr
        const uint8_t* row(int y) const;

        // 前回呼んでから変わった行の範囲 [first, last]。変わっていなければfalse
        bool takeChangedRows(int& first, int& last);

        static const char* loadErrorToStr(LoadError e);

        // ---- 途中保存(ステートセーブ) ----
        // エミュの状態(CPU・WRAM/VRAM・タイマー・MBCのバンク・RTC)+ 画面 + 音源チップの控え +
        // カートリッジRAM を1つのファイルへ書く(約23KB + セーブ領域)。置き場は /gb/states/ の
        // "<ROMの名前>.st<枠>"(枠0 = 自動 .st0、枠1 = 手動 .st1。PICO_Path::DIR::GB_STATES)。
        //   - 書き出しは一時ファイル→差し替え(書いている途中で電源が落ちても前の状態が残る)
        //   - 読み込みは**先に全体を読んで検査(大きさ・ROMの題名とチェックサム・FNV-1a)してから**今の状態へ写す
        //     (壊れたファイルで動いているゲームを壊さない)。作業用の確保はしない(512Bのスタックだけ)
        //   - gb_s を丸ごと写すので、ポインタの大きさ・構造体の配置が違うビルド(PCと実機)の間では読めない
        //     (BadFormat で断る)。関数ポインタとprivは読み込んだ後に今のものへ戻す
        //   - 音源チップは控えを読み戻し、2コア目の音源へ書き直す(鳴っていた音はゲームが次に鳴らすまで止まる)
        enum class StateError : uint8_t {
            None,
            NotLoaded,      // ROMを読んでいない/止まっている
            NoSd,
            NoPath,         // パスが長すぎる等で置き場が決まらない
            NotFound,       // 状態のファイルが無い
            WriteFailed,
            ReadFailed,
            BadFormat,      // 別のビルド/版のもの・壊れている
            OtherRom,       // 別のROMの状態
        };
        constexpr static int kStateSlots = 2;
        constexpr static int kAutoSlot = 0;
        constexpr static int kManualSlot = 1;

        StateError saveState(int slot);
        StateError loadState(int slot);
        bool hasState(int slot) const;
        // 状態のファイルのパス(ROMを読んでいなければfalse)
        bool statePath(int slot, FixedString<PICO_PATH_LEN>& out) const;
        static const char* stateErrorToStr(StateError e);

        // カートリッジRAMの書き込みが落ち着いたら .sav へ書き出す(遊んでいる途中の電池切れでも
        // ゲームの中のセーブが残るように)。最後の書き込みから kSaveIdleMs 経ち、かつ前回の書き出しから
        // kSaveMinIntervalMs 経っていれば書く。毎フレーム呼んでよい。書いたらtrue
        constexpr static uint32_t kSaveIdleMs = 2000;
        constexpr static uint32_t kSaveMinIntervalMs = 10000;
        bool flushSaveIfIdle(uint32_t now_ms);

        // 以下はPeanut-GBのコールバック(Gb_Emu.cpp内)から呼ぶ。外からは使わない
        struct Impl;
        uint8_t readRom(uint32_t addr) const;
        uint8_t readCartRam(uint32_t addr) const;
        void writeCartRam(uint32_t addr, uint8_t val);
        void drawLine(const uint8_t* pixels, int line);
        void onError(int error, uint16_t addr);
        uint8_t audioRead(uint16_t addr) const;
        void audioWrite(uint16_t addr, uint8_t val);

    private:
        Impl* impl = nullptr;

        const uint8_t* rom = nullptr;
        uint32_t rom_size = 0;
        bool rom_owned = false;   // romをmallocした(unload()でfreeする)

        uint8_t* cart_ram = nullptr;
        uint32_t cart_ram_size = 0;
        bool cart_ram_dirty = false;
        uint32_t cart_ram_last_write_ms = 0;   // flushSaveIfIdle()
        uint32_t cart_ram_last_flush_ms = 0;
        bool cart_ram_touched = false;          // 前回のflushSaveIfIdle()から書き込みがあった

        bool crashed_ = false;
        FixedString<PICO_STR_M> crash_message;

        FixedString<PICO_STR_S> title_;
        FixedString<PICO_PATH_LEN> save_path;
        FixedString<PICO_PATH_LEN> base_path;   // ROMのパスから拡張子を落としたもの
        uint16_t rom_checksum = 0;              // ヘッダの全体チェックサム(0x14E〜0x14F)

        // 音源チップのレジスタの控え(0xFF10〜0xFF3F)
        GbAudioSink* audio_sink = nullptr;
        bool audio_started = false;
        uint8_t apu_regs[0x30] = {};
        uint8_t apu_triggered = 0;      // このフレームにトリガーしたチャンネル
        uint32_t apu_last_cycle = 0;    // このフレームで最後に書いた時刻(戻らないように)
        // 前の区切り(begin/endFrame)の後に書き込みを渡した。GbAudioLink は begin/end の区切りを
        // フレームの境目にしか置けない(途中に置くと2コア目が区切りを数え違えて回り続ける)ので、
        // end() の前にこれが立っていれば先に endFrame() で閉じる
        bool audio_frame_open = false;
        uint32_t frameCycle() const;
        void audioStop();

        int changed_first = -1;
        int changed_last = -1;

        LoadError readRomFile(const char* rom_path);
        LoadError start(const char* rom_path);
        void readSaveFile();
        void resyncAudio();
};
