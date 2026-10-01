#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/ITextInputTarget.hpp"
#include "functions/KeyInput_Functions.hpp"

// オンスクリーンキーボードの「キー盤」部分の基底(Keyboard / KeyboardEng / KeyboardNum)。
//
// キー盤は画面下端に貼り付く自分の矩形(l_rect)だけを持ち、描画も当たり判定もその中だけで完結する。
// 以前は全画面を覆い、背景の斜線と上部の入力欄(Label)まで自分で描いていたが、
// その「ダイアログとしての見た目」は KeyboardDialog へ分離した。
//   - ダイアログ表示(従来どおり): KeyboardDialog が背景の斜線と入力欄を出し、その上にキー盤が載る
//   - 据え置き表示(docked)    : キー盤だけが画面下に出て、入力先(TextView等)が
//                                 onDisplayChanged() を受けて自分で表示する
// どちらにするかは KeyboardFunctions::Show() の引数で決まる。
//
// 表示/非表示は setVisible() で行い、開いたときに target->onShow()、閉じたときに target->onHide() を呼ぶ。
// 日本語⇔英字の切り替えは KeyboardFunctions::SwitchPanel() が onShow/onHide を挟まずに中身を引き継ぐ。
class KeyboardPanel : public Widget, public ITextInputWidget {
    protected:
        ITextInputTarget* target = nullptr;

        // テキスト/カーソル/変換中の読みが変わったら必ず呼ぶ。
        // text_changed=trueなら target->onTextChanged() も呼ぶ。
        // 呼んだ先で setText() 等が再入しうるので、呼び出し側はこの後で状態を触らないこと
        void notifyChanged(bool text_changed);

        // 入力を終える(決定キー)
        void submit() { this->setVisible(false); }

        // UTF-8文字列sの先頭からbyte_offsetバイトまでに何文字あるか(継続バイトは数えない)
        static int CharIndexOfByte(const char* s, size_t byte_offset) {
            int n = 0;
            for(size_t i = 0; i < byte_offset && s[i] != '\0'; i++){
                if(((uint8_t)s[i] & 0xC0) != 0x80) n++;
            }
            return n;
        }

        // ---- 物理キーボード(onPhysicalKey())から呼ばれる。各キー盤が自分の編集操作へ繋ぐ ----
        // カーソル位置へ文字列を入れる(日本語は変換中の読みを確定させてから)
        virtual void physicalInsert(const char* utf8) = 0;
        // カーソルの前の1文字を消す(先頭なら target->onBackspaceAtStart())
        virtual void physicalBackspace() = 0;
        // カーソルを動かす(端なら target->onCursorAtEdge())
        virtual void physicalMove(int delta) = 0;
        // Enter。単一行なら決定、複数行なら改行(日本語の変換中は確定だけ)
        virtual void physicalEnter();
        // このキー盤へ入れてよい文字か(数字のキー盤は英字を断る)
        virtual bool acceptsPhysicalChar(uint32_t cp) const { (void)cp; return true; }

        // 自分の高さが変わった(KeyboardNumのタブ数など)ときに呼ぶ
        void setPanelHeight(int h);

    public:
        KeyboardPanel(int panel_h) {
            this->l_rect = {0, (int16_t)(SCREEN_HEIGHT - panel_h), SCREEN_WIDTH, (int16_t)panel_h};
            this->visible = false;
        }

        // 画面下端からの高さ(据え置き表示で、入力先が自分の表示領域を縮めるのに使う)
        int getPanelHeight() const { return this->l_rect.h; }
        int getPanelTop() const { return this->l_rect.y; }

        // 表示/非表示を target へ伝える版。キーボード間の切り替えでは使わない
        void setVisible(bool visible) override;
        // target へ何も伝えずに表示/非表示だけを切り替える(KeyboardFunctions::SwitchPanel用)
        void setShownSilently(bool visible);

        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setX(int x) override {};
        void setY(int y) override {};

        void setInputTarget(ITextInputTarget* target) override {
            this->target = target;
            this->needsRender(); //決定/改行キーの表記がtargetで変わる
        }
        void removeInputTarget(ITextInputTarget* valid_target) override {
            if(this->target == valid_target) this->target = nullptr;
            this->needsRender();
        }
        ITextInputTarget* getInputTarget() override { return this->target; }

        // 物理キーボードの打鍵を入力として扱う。扱ったらtrue
        //   文字 → カーソル位置へ / Enter → 決定か改行(Ctrl+Enterは常に決定) / Esc → 決定して閉じる /
        //   Backspace・Delete・←→・Home・End → 編集。↑↓・Tab・Ctrl/Alt付きの文字は扱わない
        bool onPhysicalKey(const KeyInputFunctions::Event& ev);

        // 表示直前に呼ばれる。変換候補など、前回の入力の名残りを捨てる
        virtual void resetTransientState() {}
};
