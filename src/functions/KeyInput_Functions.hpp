#pragma once
#include <cstdint>
#include <cstddef>

// 物理キーボードの入力の窓口。
//
// 外部コントローラー(PadFunctions)が「押しているボタンの状態」を持つのに対し、こちらは
// **キーを押した出来事(1回の打鍵)の列**を持つ。文字入力は「押しっぱなし」より「何回押したか」が
// 大事なので、状態ではなく出来事で受け取る(長押しの連打はPC側のキーリピートがそのまま届く)。
//
// ---- 今の入力元: USBシリアル(PCのキーボード) ----
// PadFunctions と同じ Serial の行で届く(Serialを読むのは PadFunctions。"pad " 以外の行を
// FeedLine() へ回してくる)。1行1打鍵:
//
//     key M CODE\n
//       M    … 修飾キー(Mod のOR)の16進数1桁。1=Ctrl 2=Alt 4=Shift
//       CODE … u+XXXX(文字。Unicodeの符号位置の16進数、1〜6桁)
//              または enter backspace tab esc delete left right up down home end pageup pagedown zenhan
//
//   例) key 0 u+61   … a       key 4 u+41 … A(Shift付き。文字はShiftを反映済みで送る)
//       key 1 u+63   … Ctrl+C  key 0 u+3042 … あ(PC側のIMEで確定した文字もそのまま送れる)
//       key 0 left   … ←
//
// - 形式に合わない行は黙って捨てる。1行落ちれば1文字落ちるだけ(押しっぱなしの事故は起きない)
// - 列は kQueueSize 件の固定長。溢れた分は捨てる(DroppedCount() で数える)
//
// ---- 届け先(Update()、KeyInput_Dispatch.cpp) ----
// 1. 今の画面(Scene::onKey())。SSH(シェルへ送る)・テキストエディタ(↑↓、Ctrl+S)等が先に取る
// 2. 開いているオンスクリーンキーボードのキー盤(KeyboardPanel::onPhysicalKey())。
//    Textbox/InputDialog/チャット等、キーボードを開いて入力する所はどこでもそのまま打てる
// 3. どちらも取らなければウィジェットのフォーカスへ(Tab・矢印・Enter・Space・Esc。FocusFunctions)。それ以外は捨てる
// ただし先に:
// - 日本語入力の入り切り(半角/全角・Ctrl+Space・Tabを素早く2回)は、日本語⇔英字のキー盤の切り替えにする
// - 日本語のキー盤が読みを入力中/変換中なら、画面より先にキー盤へ配る(KeyboardPanel::wantsKeyFirst())
//
// 中身(列・行の読み取り)と配り先(Update())はファイルを分けてある。前者は何にも依存しないので、
// PadFunctions / PowerFunctions のホストテストがこちらだけをリンクすればよい。
namespace KeyInputFunctions {

    enum class Key : uint8_t {
        Char,       // cp に文字
        Enter, Backspace, Tab, Escape, Delete,
        Left, Right, Up, Down,
        Home, End, PageUp, PageDown,
        Zenhan,     // 半角/全角(日本語入力の入り切り)
    };

    enum Mod : uint8_t {
        Ctrl  = 1u << 0,
        Alt   = 1u << 1,
        Shift = 1u << 2,
    };

    struct Event {
        Key      key  = Key::Char;
        uint8_t  mods = 0;
        uint32_t cp   = 0;      // key == Char のときだけ

        bool ctrl()  const { return (mods & Ctrl)  != 0; }
        bool alt()   const { return (mods & Alt)   != 0; }
        bool shift() const { return (mods & Shift) != 0; }
        // Ctrl/Altを伴わない文字(そのまま文字として入れてよいもの)
        bool isPlainChar() const { return key == Key::Char && !ctrl() && !alt() && cp >= 0x20 && cp != 0x7F; }
    };

    constexpr size_t kQueueSize = 32;

    void Setup();

    // "key ..." の行(改行は含まない)を読む。読めればoutへ入れてtrue
    bool ParseLine(const char* line, Event& out);
    // Serialから届いた1行。"key " の行なら列へ積んでtrue(形式違いでも "key " で始まればtrue=他へ回さない)
    bool FeedLine(const char* line);
    // 列へ積む(PCビルドの入力元やテストから)。溢れたらfalse
    bool Push(const Event& ev);

    size_t Pending();               // 列に溜まっている数
    bool Pop(Event& out);           // 先頭を取り出す
    void DiscardPending();          // 全部捨てる(スリープから起こした打鍵を画面へ渡さない)
    uint32_t DroppedCount();

    // 今の画面 → 開いているキー盤 の順に配る(loop()でシーン遷移の適用の後、ウィジェット更新の前)。
    // 実装は KeyInput_Dispatch.cpp
    void Update();
    // 直前の Update() で1つでも配ったか(打っている間は自動調光/スリープさせない)
    bool HadInputThisFrame();

    // Tabを2回押したとみなす間隔
    constexpr uint32_t kDoubleTabMs = 400;
    // 日本語入力の入り切りの打鍵か(半角/全角・Ctrl+Space・素早い2回目のTab)。
    // Tabの1回目はfalse(そのまま配る)で、時刻を覚える。他の打鍵を挟むと数え直し
    bool CheckImeToggle(const Event& ev, uint32_t now_ms);

    // 符号位置 → UTF-8。書いたバイト数(0=表せない)。outは5バイト以上(終端を付ける)
    int EncodeUtf8(uint32_t cp, char* out);

    namespace detail {
        // Update() が配った数を記録する(HadInputThisFrame()用。KeyInput_Dispatch.cppが呼ぶ)
        void SetDispatchedThisFrame(bool any);
    }
}
