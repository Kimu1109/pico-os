#pragma once

#include "gui/widgets/Widget.hpp"
#include "services/Lock_Functions.hpp"
#include "consts.hpp"

namespace KeyInputFunctions { struct Event; }

// 画面ロックのオーバーレイ(LockFunctions)。画面全体を覆い、タッチを全部取る。
// 子を持たずrender()で直接描き、タップの位置からキーを逆算する型(KeyboardNum/AppGridと同じ)。
//
//   12:34            … 時刻
//   10月11日(日)
//   PINを入力         … LockFunctions::Message()(間違えたら赤)
//   ● ● ● ○          … 打った桁数(PIN自体は出さない)
//   [アラームを止める]  … アラーム/タイマーが鳴っているときだけ
//   [1][2][3]
//   [4][5][6]
//   [7][8][9]
//   [⌫][0][OK]       … 何も打っていないとき、変更の途中なら⌫が「取消」
class LockScreen : public Widget {
    public:
        static constexpr int kKeyTop = 134;
        static constexpr int kKeyH = 43;
        static constexpr int kKeyGap = 3;
        static constexpr int kRingY = 107;
        static constexpr int kRingH = 22;

        // キーの番号: 0〜8 = "1"〜"9"、9 = 消す/取消、10 = "0"、11 = OK、12 = 止める、-1 = 無し
        static constexpr int kKeyBack = 9, kKeyZero = 10, kKeyOk = 11, kKeyRing = 12;

        LockScreen(){
            this->l_rect = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        }

        // 打った桁を空にする
        void reset();
        // LockFunctionsの状態(文言・Step)が変わった
        void onStateChanged(){ this->needsRender(); }
        // 毎フレーム(表示中だけ)。時刻・待ち時間・鳴っているかの表示を進める
        void tick();
        void onKey(const KeyInputFunctions::Event& ev);
        // テスト用: キーを押したことにする
        void pressKey(int key);
        int keyAt(int x, int y) const;
        int enteredLength() const { return this->len; }

        void causeOnPressStart() override;
        void causeOnPressMove() override;
        void causeOnPressEnd() override;
        void render() override;

        bool hitTest(int, int) override { return this->visible; }   //画面全体を塞ぐ
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        WidgetType getWidgetType() const override { return WidgetType::LockScreen; }

    private:
        char pin[LockFunctions::kMaxPin + 1] = "";
        int len = 0;
        int pressed_key = -1;
        int shown_minute = -1;
        unsigned long shown_wait_sec = 0;
        bool shown_ringing = false;

        void keyRect(int key, int& x, int& y, int& w, int& h) const;
        void submit();
};
