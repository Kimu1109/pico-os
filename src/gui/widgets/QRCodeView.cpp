#include "gui/widgets/QRCodeView.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "OS_Data.hpp"

#include <cstring>

namespace {
    // 符号化の作業場所。符号化の間しか使わないので全部のQRCodeViewで1本を使い回す
    // (1コア目だけが触る。2コア目は音声専用)
    uint8_t g_temp[QRCodeView::kBufLen];

    // パレットの色(RGB565)のおおよその明るさ(0〜約190)
    int Luma(int8_t index) {
        const int c = PICO_GFX::COLORS[index & 0x0F] & 0xFFFF;
        const int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        return r * 2 * 3 + g * 6 + b * 2;   // r・bを6bitへ揃えて 3:6:1 で重み付け
    }
}

bool QRCodeView::setText(const char* text, size_t len) {
    if (!text) { text = ""; len = 0; }
    bool ok = len <= kMaxTextLen && std::memchr(text, '\0', len) == nullptr;
    if (ok) {
        this->text.assign(text, len);
        ok = this->encode();
    } else {
        this->text.clear();
        this->encoded = false;
    }
    this->needsRender();
    return ok;
}

bool QRCodeView::setText(const char* text) {
    return this->setText(text, text ? std::strlen(text) : 0);
}

bool QRCodeView::setEcc(Ecc e) {
    if ((uint8_t)e > (uint8_t)Ecc::High) return false;
    this->ecc = e;
    const bool ok = this->encode();
    this->needsRender();
    return ok;
}

bool QRCodeView::encode() {
    if (this->text.length() == 0) {
        this->encoded = false;
        return true;
    }
    // マスクは8種を評価して一番読みやすいものを選ぶ。誤り訂正は升目の数が増えない範囲で強める
    this->encoded = qrcodegen_encodeText(this->text.c_str(), g_temp, this->qr,
        (qrcodegen_Ecc)this->ecc, qrcodegen_VERSION_MIN, kMaxVersion, qrcodegen_Mask_AUTO, true);
    return this->encoded;
}

void QRCodeView::render() {
    if (!this->needs_redraw) return;
    if (!this->visible) return;

    if (this->prev_l_rect != this->l_rect)
        markdirty(getScreenPrevRect());

    const Rect g = this->getScreenRect();
    markdirty(g);

    // 明るい方を地に(クラスのコメント参照)
    int8_t light = this->background_color;
    int8_t dark = this->color;
    if (Luma(dark) > Luma(light)) { const int8_t t = light; light = dark; dark = t; }

    OSData::frame->fillRect(g.x, g.y, g.w, g.h, light);

    const int n = this->getModuleCount();
    if (n > 0) {
        const int side = g.w < g.h ? g.w : g.h;
        // 升を大きくするのを先にする: 余白1升で入る一番大きい升の大きさを決め、その大きさのまま
        // 入る範囲で余白を規格の4升へ近づける(小さい升は読み取りにくい。余白の外も地の色なら
        // 読み取り機は困らない)
        const int scale = side / (n + 2);
        int quiet = kQuietZone;
        while (quiet > 1 && scale > 0 && side / (n + quiet * 2) < scale) quiet--;
        if (scale > 0) {
            const int ox = g.x + (g.w - n * scale) / 2;
            const int oy = g.y + (g.h - n * scale) / 2;
            // 横に続く黒い升はまとめて1回で塗る
            for (int y = 0; y < n; y++) {
                int x = 0;
                while (x < n) {
                    if (!qrcodegen_getModule(this->qr, x, y)) { x++; continue; }
                    const int start = x;
                    while (x < n && qrcodegen_getModule(this->qr, x, y)) x++;
                    OSData::frame->fillRect(ox + start * scale, oy + y * scale,
                        (x - start) * scale, scale, dark);
                }
            }
        }
    }

    this->prev_l_rect.copy(this->l_rect);
    this->needs_redraw = false;
}
