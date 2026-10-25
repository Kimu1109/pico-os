#pragma once

#include <cstddef>
#include <cstdint>
#include "gui/widgets/Widget.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"
#include "qrcodegen.h"

// QRコードの表示部品(汎用。Luaからは pico.create("QRCode"))。
// setText()の文字列を符号化した結果(白黒の升目)だけを固定長の配列に持ち、render()で
// l_rectの中央へ正方形に描く。升目1つの大きさは「升目の数+周りの余白1升」が短い辺に収まる
// 最大の整数倍で、その大きさのまま入るなら余白を規格の4升まで取る。タッチには反応しない表示専用。
//
// 符号化は lib/qrcodegen(lib/qrcodegen/README-pico-os.md)。数字だけ・英大文字と一部の記号だけの
// 文字列は自動で詰めた形になり、それ以外(日本語を含む)はUTF-8のバイト列のまま入る。
// 255バイトまでなら誤り訂正のどの段でも入る(kMaxVersionはそこから決めた)。
//
// 色: color(既定 黒)が升目の色、background_color(既定 白)が地の色。ただし描くときは
// パレットで明るい方を地にする(ダークテーマで0と15の明暗が入れ替わっても、反転したQRを
// 読めない読み取り機があるので、常に「明るい地に暗い升目」になるようにする)。
class QRCodeView : public Widget {
    public:
        // 誤り訂正の強さ(qrcodegen_Ecc と同じ並び。強いほど汚れ・欠けに強いが升目が増える)
        enum class Ecc : uint8_t { Low = 0, Medium = 1, Quartile = 2, High = 3 };

        constexpr static size_t kMaxTextLen = PICO_PATH_LEN;   // 255バイト
        constexpr static int kMaxVersion = 17;                 // 85x85升。255バイトが誤り訂正Highでも入る
        constexpr static size_t kBufLen = qrcodegen_BUFFER_LEN_FOR_VERSION(kMaxVersion);
        constexpr static int kQuietZone = 4;                   // 規格の余白(升の数)。入らなければ1升まで削る

        QRCodeView(int16_t x, int16_t y, int16_t size) {
            this->l_rect = {x, y, size, size};
            this->hit_transparent = true;
            this->background_color = PICO_WHITE;
        }

        void render() override;

        WidgetType getWidgetType() const override { return WidgetType::QRCodeView; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void setW(int w) { this->l_rect.w = (int16_t)w; this->needsRender(); }
        void setH(int h) { this->l_rect.h = (int16_t)h; this->needsRender(); }

        // 符号化して描き直す。長すぎて入らないときはfalse(升目は空になり、地の色だけを描く)。
        // ""は空(何も描かない)にするだけでtrue
        bool setText(const char* text, size_t len);
        bool setText(const char* text);
        const char* getText() const { return this->text.c_str(); }

        Ecc getEcc() const { return this->ecc; }
        bool setEcc(Ecc e);   // 今の文字列で符号化し直す(入らなくなればfalse)

        int8_t getColor() const { return this->color; }
        void setColor(int8_t c) { this->color = c; this->needsRender(); }

        // 1辺の升目の数(21〜85)。空・符号化に失敗したときは0
        int getModuleCount() const { return this->encoded ? qrcodegen_getSize(this->qr) : 0; }
        // (x,y)の升目が黒か(範囲外はfalse)
        bool getModule(int x, int y) const {
            return this->encoded && qrcodegen_getModule(this->qr, x, y);
        }

    private:
        bool encode();

        FixedString<kMaxTextLen + 1> text;   // FixedString<N>はN-1バイトまで
        uint8_t qr[kBufLen];
        Ecc ecc = Ecc::Medium;
        bool encoded = false;
        int8_t color = PICO_BLACK;
};
