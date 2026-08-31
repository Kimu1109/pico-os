#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "config/LGFX_Config.hpp"
#include "OS_Data.hpp"
#include <SPI.h>

#include "GFX_Functions.hpp"

void PICO_GFX::Setup() {
    OSData::lcd->init();
    OSData::lcd->setBaseColor(TFT_WHITE);
    OSData::lcd->clear(TFT_WHITE);
    OSData::lcd->setFont(&lgfxJapanGothicP_24);
    OSData::lcd->setTextColor(TFT_BLACK);

    LGFX_Sprite* frame = new LGFX_Sprite(OSData::lcd);
    frame->setColorDepth(4);
    frame->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    for(int i = 0; i < 16; i++){
        frame->setPaletteColor(i, COLORS[i]);
    }
    frame->setBaseColor(PICO_WHITE);
    frame->clear(PICO_WHITE);
    frame->setFont(&lgfxJapanGothicP_24);
    frame->setTextColor(PICO_BLACK);
    frame->setTextWrap(false, false);
    OSData::frame = frame;
    MarkDirty({0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});

    pinMode(22, OUTPUT); //LED ON
    digitalWrite(22, HIGH);

    dirtyRects.reserve(48);
    isDirtyDeactivates = false;

    LOG_SYS_OK("GFX Setup has succeeded!");
}

void PICO_GFX::MarkDirty(const Rect& rect) {
    if(!isDirtyDeactivates)
        dirtyRects.push_back(rect);
}

void PICO_GFX::FlushDirty() {
    if (dirtyRects.empty()) return;

    //! DEBUG !
    unsigned long timer_start_ms = millis();
    unsigned long buf_timer_ms = 0;
    int draw_frame_total_ms = 0;
    int push_frame_total_ms = 0;
    //! DEBUG !

    for (auto& d : dirtyRects) {
        buf_timer_ms = millis(); //! DEBUG !

        std::vector<Widget*> hit;
        for (auto* w : WidgetFunctions::widgets) {
            if (w && w->getVisible() && w->clippedScreenRect().intersects(d)) hit.push_back(w);
        }
        for (int k = (int)WidgetFunctions::dialog_roots.size() - 1; k >= 0; k--) {
            Widget* root = WidgetFunctions::dialog_roots[k];
            if (!root || !root->getVisible()) continue;
            root->visitAll([&hit, &d](Widget* w) {
                if (w && w->getVisible() && w->clippedScreenRect().intersects(d)) hit.push_back(w);
            });
        }

        // 描画開始インデックスと背景クリア要否の決定（手前から奥へスキャン）
        size_t start_idx = 0;
        bool clear_bg = true;

        for (int i = (int)hit.size() - 1; i >= 0; --i) {
            Widget* w = hit[i];
            WidgetTools::RenderMode mode = w->getRenderMode();

            if (mode == WidgetTools::TRANSLUCENT) {
                // TRANSLUCENT: 背景ウィジェットの更新を行わない
                start_idx = i;
                clear_bg = false;
                break;
            }
            else if (mode == WidgetTools::OPAQUE && w->clippedScreenRect().contains(d)) {
                // OPAQUE かつ Dirty領域全体を覆っている場合、下位ウィジェット描画および背景白クリアをスキップ
                start_idx = i;
                clear_bg = false;
                break;
            }
        }

        // ★ 1. 必要な場合のみ背景を白クリア
        if (clear_bg) {
            OSData::frame->fillRect(d.x, d.y, d.w, d.h, PICO_BACKGROUND);
        }

        // ★ 2. start_idx から上へ Widget を重ね描きする
        isDirtyDeactivates = true;
        for (size_t i = start_idx; i < hit.size(); ++i) {
            Rect clip = hit[i]->clippedScreenRect().intersection(d);
            if (clip.w <= 0 || clip.h <= 0) continue;
            OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);
            if (hit[i]->getRenderMode() == WidgetTools::OPAQUE) {
                OSData::frame->fillRect(clip.x, clip.y, clip.w, clip.h, hit[i]->getBackgroundColor());
            }
            hit[i]->renderForce();
            OSData::frame->clearClipRect();
        }
        isDirtyDeactivates = false;

        draw_frame_total_ms += millis() - buf_timer_ms;
        buf_timer_ms = millis();

        // ★ 3. 液晶へ転送
        OSData::lcd->setClipRect(d.x, d.y, d.w, d.h);
        OSData::frame->pushSprite(OSData::lcd, 0, 0);
        OSData::lcd->clearClipRect();

        push_frame_total_ms += millis() - buf_timer_ms;
    }

    if(enableDirectRender){
        OSData::lcd->setClipRect(
            directRenderRect.x, directRenderRect.y,
            directRenderRect.w, directRenderRect.h
        );
        OSData::frame->pushSprite(OSData::lcd, 0, 0);
        OSData::lcd->clearClipRect();
    }

    Serial.printf(
        "dirtyrects count: %d, draw average: %dms, push average: %dms, draw total: %dms, push total: %dms\n",
        dirtyRects.size(),
        draw_frame_total_ms / dirtyRects.size(),
        push_frame_total_ms / dirtyRects.size(),
        draw_frame_total_ms,
        push_frame_total_ms
    );

    dirtyRects.clear();
}

void PICO_GFX::DrawDialogBackground(){
    constexpr int16_t kSpacing   = 6;  // 斜線の間隔(px)
    constexpr uint8_t  kColor    = PICO_BLACK;

    for (int16_t y = 0; y < SCREEN_HEIGHT; ++y) {
        // このy行で最初にヒットするxオフセットを計算
        int16_t offset = ((kSpacing - (y % kSpacing)) % kSpacing);
        for (int16_t x = offset; x < SCREEN_WIDTH; x += kSpacing) {
            OSData::frame->writePixel(x , y, kColor);
            OSData::frame->writePixel(x, SCREEN_HEIGHT - y, kColor);
        }
    }
}