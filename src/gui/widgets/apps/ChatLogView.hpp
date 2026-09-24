#pragma once

#include "gui/widgets/Widget.hpp"
#include "gui/widgets/interfaces/IBorderColor.hpp"
#include "chat/Chat_Proto.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include <cstdint>

// チャットの発言の一覧(名前と時刻の行 + 折り返した本文)。指でなぞって縦にスクロールする。
//
// 子ウィジェットは持たず render() でframeへ直接描く(MonthGrid / AppGrid と同じ方式)。
// 発言ごとに Label を new すると、1件あたり数百B + 行データの確保が発言の数だけ走るため。
//
// 発言そのものは持たず、ChatProto::IMessageSource(ChatClient)を読むだけ。
// 中身が変わったらシーンが refresh() を呼ぶ。**折り返しの計算(1文字ごとの幅の測定)は
// 発言1件につき1回だけ**で、結果の高さを id ごとに覚えておく(新着1件で全件を測り直さない)。
// 描くときは見えている発言だけをもう一度折り返す。
//
// 一番下を見ている間に新着が来たら一番下へ付いていく。上へ遡って読んでいる間は動かさない。
class ChatLogView : public Widget, public IBorderColor {
    public:
        constexpr static int kMaxEntries = 32;

        ChatLogView(int x, int y, int w, int h){
            this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h };
        }

        void setSource(const ChatProto::IMessageSource* source){ this->source = source; }

        // 高さを変える。折り返しの幅は変わらないので、測った高さはそのまま使える
        void setH(int h){
            this->l_rect.h = (int16_t)h;
            if(this->scroll_y > this->maxScroll() || this->stick_bottom) this->scroll_y = this->maxScroll();
            this->needsRender();
        }

        // 発言が変わったら呼ぶ。一番下を見ていたら一番下へ付いていく
        void refresh();
        // 発言が無いときに真ん中へ出す文("読み込み中…" 等)
        void setPlaceholder(const char* text);
        void scrollToBottom();

        int getScrollY() const { return this->scroll_y; }
        bool isFollowingBottom() const { return this->stick_bottom; }
        // これまでに折り返しを計算した発言の数(控えが効いているかの確認用)
        int measuredCount() const { return this->measured; }

        void render() override;
        void causeOnPressStart() override;
        void causeOnPressMove() override;

        WidgetType getWidgetType() const override { return WidgetType::ChatLogView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setBorderColor(int8_t palette_color) override {
            this->border_color = palette_color;
            this->needsRender();
        }

        // 名前の色(名前から決まる。同じ人はいつも同じ色)
        static int8_t NameColor(const char* name);

        // text[start..] から、幅 max_w に収まる1行の終わり(バイト位置)を返す。
        // next には次の行の始まりが入る(改行で切れた場合は改行の次)。
        // 1行は必ず1文字以上進む。現在のフォントの幅で測る
        static int WrapLine(const char* text, int len, int start, int max_w, int& next);

    private:
        const ChatProto::IMessageSource* source = nullptr;

        // 発言ごとの高さの控え(idで引く)。並びは source と同じ
        uint32_t ids[kMaxEntries] = {};
        int16_t heights[kMaxEntries] = {};
        int count = 0;
        int total_h = 0;
        int measured = 0;

        int scroll_y = 0;       // 中身の先頭から、表示の上端までの距離
        bool stick_bottom = true;

        int ref_touch_y = 0;
        int ref_scroll_y = 0;

        FixedString<PICO_STR_M> placeholder;

        int lineH() const;
        int textWidth() const;  // 本文を折り返す幅
        int maxScroll() const;
        int measure(const ChatProto::Message& m) const; // フォントは呼び出し側で設定済み
        void formatHeader(const ChatProto::Message& m, char* out, size_t cap) const;
};
