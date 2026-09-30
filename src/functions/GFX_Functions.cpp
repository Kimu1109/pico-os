#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "config/LGFX_Config.hpp"
#include "OS_Data.hpp"
#include <SPI.h>

#include "GFX_Functions.hpp"

namespace {
    // 起動直後(SetBrightness()が一度も呼ばれる前)は満光。DisplayFunctions::Setup()が
    // display.cfgを読んだ直後に上書きする
    uint8_t currentBrightness = 100;
}

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

    //バックライト(TFT_LED)はLGFXのLight_PWMがinit()で初期化済み。ここでpinMode()/digitalWrite()を
    //すると端子がPWMからSIOへ戻って調光が効かなくなるので触らない(明るさはSetBrightness()で決める)

    isDirtyDeactivates = false;

    LOG_SYS_OK("GFX Setup has succeeded!");
}

// 画面の明るさ: バックライト(TFT_LED)のPWMデューティ比を変える。frameのパレットには触れないので、
// CanvasRaster等の自前スプライトを含め画面全体が一様に暗くなり、再描画も要らない。
// LovyanGFXのrp2040のLight_PWMはPWMのwrapを100に固定している(カウンタは0〜100の101段)ため、
// 0〜99はそのままデューティ比(%)になり、100以上は常時HIGH。百分率の100だけ255を渡して
// 「101段中の100段(約99%)」ではなく完全に点灯させる。
// PC/Webビルドは同じ0〜100の値をSDLのテクスチャの色の掛け率へ変える(Panel_sdl_SpiWait参照)。
void PICO_GFX::SetBrightness(uint8_t percent) {
    if(percent > 100) percent = 100;
    currentBrightness = percent;

    if(!OSData::lcd) return; // Setup()より前(呼ばれない想定だが念のため)

    OSData::lcd->setBrightness(percent >= 100 ? 255 : percent);
}

uint8_t PICO_GFX::GetBrightness() {
    return currentBrightness;
}

void PICO_GFX::MarkDirty(const Rect& rect) {
    if(isDirtyDeactivates) return;

    //面積ゼロの矩形は描くものが無いのに、FlushDirty()で全ウィジェットの当たり判定と
    //pushSprite()を1周ぶん走らせてしまう。Labelは「消すべき古い領域」として
    //未使用のカーソル矩形({0,0,0,0})を毎回markdirtyするので、ここで落とす
    if(rect.w <= 0 || rect.h <= 0) return;

    //既に上限を超えて「画面全体を1枚として転送する」方針に切り替わっている間は、
    //個々の矩形を追っても無駄なので静かに捨てる
    if(dirtyOverflowed) return;

    if(dirtyRectCount >= kMaxDirtyRects) {
        //128件を超えたら、個々の矩形を保持するのを諦めて画面全体を1枚のdirty矩形として
        //扱う方針へ切り替える(実際に置き換えるのはFlushDirty()側)。ここまでに積んだ
        //個別の矩形はどうせ全画面転送に飲み込まれるので、保持し続ける意味が無く破棄する
        dirtyOverflowed = true;
        dirtyRectCount = 0;
        return;
    }

    dirtyRects[dirtyRectCount++] = rect;
}

void PICO_GFX::FlushDirty() {
    if (dirtyRectCount == 0 && !dirtyOverflowed) return;

    //128件を超えた場合は、細切れの矩形を1枚ずつ処理する代わりに画面全体を
    //1枚のdirty矩形として扱う(取りこぼしが無く、128枚の当たり判定・転送より軽い)
    if (dirtyOverflowed) {
        dirtyRects[0] = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        dirtyRectCount = 1;
    }

    //! DEBUG !
    unsigned long buf_timer_ms = 0;
    int draw_frame_total_ms = 0;
    int push_frame_total_ms = 0;
    //! DEBUG !

    for (int dirty_i = 0; dirty_i < dirtyRectCount; dirty_i++) {
        const Rect& d = dirtyRects[dirty_i];
        buf_timer_ms = millis(); //! DEBUG !

        std::vector<Widget*> hit;
        for (auto* w : WidgetFunctions::widgets) {
            if (w && w->getVisible() && w->clippedScreenRect().intersects(d)) hit.push_back(w);
        }
        for (size_t k = 0; k < WidgetFunctions::dialog_roots.size(); k++) {
            Widget* root = WidgetFunctions::dialog_roots[k];
            if (!root || !root->getVisible()) continue;
            root->visitAll([&hit, &d](Widget* w) {
                if (w && w->getVisible() && w->clippedScreenRect().intersects(d)) hit.push_back(w);
            });
        }
        for (size_t k = 0; k < WidgetFunctions::overlays.size(); k++) {
            Widget* root = WidgetFunctions::overlays[k];
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

    //! DEBUG !
    //レンダリングが発生したフレーム(dirtyRectsが空でない呼び出し)のみを対象に
    //パフォーマンスを積算し、5秒に1回だけ平均をシリアルに出力する
    static unsigned long perf_report_start_ms = millis();
    static unsigned long perf_draw_total_ms = 0;
    static unsigned long perf_push_total_ms = 0;
    static unsigned long perf_dirtyrects_total = 0;
    static unsigned long perf_frame_count = 0;

    constexpr unsigned long PERF_REPORT_INTERVAL_MS = 5000;

    perf_draw_total_ms += draw_frame_total_ms;
    perf_push_total_ms += push_frame_total_ms;
    perf_dirtyrects_total += dirtyRectCount;
    perf_frame_count++;

    const unsigned long now_ms = millis();
    const unsigned long elapsed_ms = now_ms - perf_report_start_ms;

    if (elapsed_ms >= PERF_REPORT_INTERVAL_MS) {
        const float avg_fps = perf_frame_count * 1000.0f / elapsed_ms;

        Serial.printf(
            "fps: %.1f, dirtyrects average: %.1f, draw average: %lums, push average: %lums\n",
            avg_fps,
            (float)perf_dirtyrects_total / perf_frame_count,
            perf_draw_total_ms / perf_dirtyrects_total,
            perf_push_total_ms / perf_dirtyrects_total
        );

        perf_report_start_ms = now_ms;
        perf_draw_total_ms = 0;
        perf_push_total_ms = 0;
        perf_dirtyrects_total = 0;
        perf_frame_count = 0;
    }
    //! DEBUG !

    dirtyRectCount = 0;
    dirtyOverflowed = false;
}

void PICO_GFX::DrawDialogBackground(){
    constexpr int16_t kSpacing = 6;
    constexpr uint8_t kColor   = PICO_BLACK;

    const int16_t x0 = 0, y0 = 0;
    const int16_t x1 = SCREEN_WIDTH - 1, y1 = SCREEN_HEIGHT - 1;

    auto floorToSpacing = [](int16_t v){
        int16_t m = v % kSpacing;
        return (m < 0) ? v - (m + kSpacing) : v - m;
    };

    // ---- \ 方向: x + y = 一定(6の倍数) ----
    for (int16_t s = floorToSpacing(x0 + y0); s <= x1 + y1; s += kSpacing) {
        int16_t lx0 = std::max<int16_t>(x0, s - y1);
        int16_t lx1 = std::min<int16_t>(x1, s - y0);
        if (lx0 > lx1) continue;
        OSData::frame->drawLine(lx0, s - lx0, lx1, s - lx1, kColor);
    }

    // ---- / 方向: 元コードの SCREEN_HEIGHT - y を x - y の式に読み替え ----
    for (int16_t d = floorToSpacing(x0 - (SCREEN_HEIGHT - y1)); 
         d <= x1 - (SCREEN_HEIGHT - y0); d += kSpacing) {
        int16_t lx0 = std::max<int16_t>(x0, d + (SCREEN_HEIGHT - y1));
        int16_t lx1 = std::min<int16_t>(x1, d + (SCREEN_HEIGHT - y0));
        if (lx0 > lx1) continue;
        OSData::frame->drawLine(lx0, SCREEN_HEIGHT - (lx0 - d), lx1, SCREEN_HEIGHT - (lx1 - d), kColor);
    }
}