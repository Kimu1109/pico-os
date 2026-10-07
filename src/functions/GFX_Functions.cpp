#include "functions/Widget_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "config/LGFX_Config.hpp"
#include "OS_Data.hpp"
#include <SPI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

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

    dirtyForceBelow[dirtyRectCount] = false;
    dirtyRects[dirtyRectCount++] = rect;
}

namespace {
    uint32_t perf_pushed_px = 0;   // 液晶へ送った画素数(5秒ごとの集計)
    uint32_t perf_skipped_px = 0;  // dirtyだったが中身が同じで送らなかった画素数

    // 2つの矩形をまとめる目安。別々に描いて送るより、間の余白ごと1枚にしたほうが安い大きさ
    // (矩形1枚ごとに、全ウィジェットの当たり判定・Luaのrender・液晶の範囲指定の手間がかかる)
    constexpr int32_t kMergeSlackPx = 1024;

    int32_t Area(const Rect& r) { return (int32_t)r.w * r.h; }
    Rect Union(const Rect& a, const Rect& b) {
        const int16_t x0 = std::min(a.x, b.x), y0 = std::min(a.y, b.y);
        const int16_t x1 = std::max<int16_t>(a.x + a.w, b.x + b.w), y1 = std::max<int16_t>(a.y + a.h, b.y + b.h);
        return Rect{x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0)};
    }

    // ---- 行ごとの「液晶に今出ている内容」 ----
    // FlushDirty()が終わった時点で液晶の中身はframeと同じ(全部の変化をdirty矩形として送るため)。
    // そこで送った行のframeの中身のハッシュを覚えておき、次に同じ行がdirtyになっても中身が
    // 同じなら送らない。スクロールで空だけの行・動いていないボタンの帯・再描画しても同じ絵の
    // 部分を送らずに済む。64bitのハッシュなので、違う中身を同じと見る(古い絵が残る)ことは実質無い。
    // 液晶へframeを通さずに描いたとき(Luaデバッガの画面)はMarkDirtyBelow()の矩形が必ず送られる。
    uint64_t row_hash[SCREEN_HEIGHT];
    bool     row_valid[SCREEN_HEIGHT];
    uint64_t row_new[SCREEN_HEIGHT];
    bool     row_computed[SCREEN_HEIGHT];

    uint64_t HashRow(int y) {
        const uint8_t* buf = static_cast<const uint8_t*>(OSData::frame->getBuffer());
        const int stride = (OSData::frame->width() * 4 + 7) / 8; // 4bpp
        const uint8_t* p = buf + (size_t)y * stride;
        uint32_t a = 0x9E3779B9u, b = 0x85EBCA6Bu;
        int i = 0;
        for (; i + 4 <= stride; i += 4) {
            uint32_t w;
            memcpy(&w, p + i, 4);
            a = (a ^ w) * 0x01000193u;
            b = ((b ^ w) * 0xC2B2AE35u) ^ (b >> 15);
        }
        for (; i < stride; i++) a = (a ^ p[i]) * 0x01000193u;
        return ((uint64_t)a << 32) | b;
    }

    bool RowChanged(int y) {
        if (!row_computed[y]) {
            row_new[y] = HashRow(y);
            row_computed[y] = true;
        }
        return !row_valid[y] || row_hash[y] != row_new[y];
    }
}

void PICO_GFX::InvalidateLcdRows(int y, int h) {
    for (int i = std::max(0, y); i < std::min<int>(SCREEN_HEIGHT, y + h); i++) row_valid[i] = false;
}

void PICO_GFX::BeginRowCompare() {
    memset(row_computed, 0, sizeof(row_computed));
}

void PICO_GFX::EndRowCompare() {
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        if (!row_computed[y]) continue;
        row_hash[y] = row_new[y];
        row_valid[y] = true;
    }
}

uint32_t PICO_GFX::PushChangedRows(const Rect& d, bool force) {
#if defined(PICOOS_PC)
    // PCビルドの比較用: PICOOS_NO_ROW_SKIP=1 なら最適化前と同じく矩形を全部送る
    static const bool no_skip = getenv("PICOOS_NO_ROW_SKIP") != nullptr;
    if (no_skip) force = true;
#endif
    // 変わった行が続く帯ごとに送る。間に挟まる変わっていない行が数行なら、帯を分けずに一緒に送る
    // (帯を分けるたびに液晶の範囲指定が要るため)
    constexpr int kGapRows = 2;
    uint32_t pushed = 0;
    const int end = d.y + d.h;
    int y = d.y;
    while (y < end) {
        if (!force && !RowChanged(y)) { perf_skipped_px += d.w; y++; continue; }
        const int y0 = y;
        int last = y; // 変わった最後の行
        for (y = y0 + 1; y < end; y++) {
            if (force || RowChanged(y)) last = y;
            else if (y - last > kGapRows) break;
        }
        const int16_t h = (int16_t)(last + 1 - y0);
        OSData::lcd->setClipRect(d.x, y0, d.w, h);
        OSData::frame->pushSprite(OSData::lcd, 0, 0);
        OSData::lcd->clearClipRect();
        pushed += (uint32_t)d.w * h;
        perf_skipped_px += (uint32_t)d.w * (y - 1 - last);
        y = last + 1;
    }
    if (force) {
        // 送った行は計算済みにしておく(EndRowCompare()で覚える)
        for (int r = d.y; r < end; r++) RowChanged(r);
    }
    return pushed;
}

void PICO_GFX::CoalesceDirtyRects() {
    const Rect screen{0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
    int n = 0;
    for (int i = 0; i < dirtyRectCount; i++) {
        const Rect r = dirtyRects[i].intersection(screen);
        if (r.w <= 0 || r.h <= 0) continue;
        dirtyForceBelow[n] = dirtyForceBelow[i];
        dirtyRects[n++] = r;
    }
    // 重なる・近い2枚を1枚にする。まとめた結果がまた別の矩形と重なりうるので、変わらなくなるまで繰り返す
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < n && !merged; i++) {
            for (int j = i + 1; j < n; j++) {
                const Rect& a = dirtyRects[i];
                const Rect& b = dirtyRects[j];
                const Rect u = Union(a, b);
                if (a.intersects(b) || Area(u) <= Area(a) + Area(b) + kMergeSlackPx) {
                    dirtyRects[i] = u;
                    dirtyForceBelow[i] = dirtyForceBelow[i] || dirtyForceBelow[j];
                    dirtyRects[j] = dirtyRects[n - 1];
                    dirtyForceBelow[j] = dirtyForceBelow[n - 1];
                    n--;
                    merged = true;
                    break;
                }
            }
        }
    }
    dirtyRectCount = n;
}

#if defined(PICOOS_PC)
void PICO_GFX::VerifyLcdMatchesFrame() {
    static uint32_t checks = 0, bad_frames = 0;
    static uint16_t lcd_row[SCREEN_WIDTH], frame_row[SCREEN_WIDTH];
    uint32_t bad = 0;
    int first_y = -1;
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        OSData::lcd->readRect(0, y, SCREEN_WIDTH, 1, lcd_row);
        OSData::frame->readRect(0, y, SCREEN_WIDTH, 1, frame_row);
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            if (lcd_row[x] != frame_row[x]) { if (first_y < 0) first_y = y; if (bad < 8) Serial.printf("  (%d,%d) lcd=%04x frame=%04x\n", x, y, lcd_row[x], frame_row[x]); bad++; }
        }
    }
    checks++;
    if (bad) {
        bad_frames++;
        Serial.printf("[VERIFY] 液晶とframeが%lu画素食い違っています(最初の行 y=%d)\n", (unsigned long)bad, first_y);
    }
    if (checks % 300 == 0) Serial.printf("[VERIFY] %lu回確認、食い違い %lu回\n", (unsigned long)checks, (unsigned long)bad_frames);
}
#endif

void PICO_GFX::FlushDirty() {
    // パレットが変わったら、frameのパレットを合わせて全画面を描き直す。
    // frameの中身(パレット番号)は変わらないので、行を飛ばす最適化も効かせないよう全行を無効にする
    static uint32_t applied_palette_revision = 1;
    if (applied_palette_revision != paletteRevision && OSData::frame) {
        applied_palette_revision = paletteRevision;
        for (int i = 0; i < 16; i++) OSData::frame->setPaletteColor(i, COLORS[i]);
        InvalidateLcdRows(0, SCREEN_HEIGHT);
        MarkDirtyBelow({0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
    }

    if (dirtyRectCount == 0 && !dirtyOverflowed) return;

    //128件を超えた場合は、細切れの矩形を1枚ずつ処理する代わりに画面全体を
    //1枚のdirty矩形として扱う(取りこぼしが無く、128枚の当たり判定・転送より軽い)
    if (dirtyOverflowed) {
        dirtyRects[0] = {0, 0, SCREEN_WIDTH, SCREEN_HEIGHT};
        dirtyForceBelow[0] = dirtyForceBelowAny;
        dirtyRectCount = 1;
    }

    //! DEBUG !
    unsigned long buf_timer_ms = millis();
    int draw_frame_total_ms = 0;
    int push_frame_total_ms = 0;
    //! DEBUG !

    // ★ 0. 重なる/近いdirty矩形を1枚にまとめる(同じ所を2回描いて2回送らないため)
    CoalesceDirtyRects();

    // ★ 1. 全部の矩形をframeへ描く(液晶へ送るのは全部描き終えてから)
    for (int dirty_i = 0; dirty_i < dirtyRectCount; dirty_i++) {
        const Rect& d = dirtyRects[dirty_i];
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
                // MarkDirtyBelow()の矩形は半透明の下も描き直す(上に重ねていたものを消した跡)
                if (dirtyForceBelow[dirty_i]) continue;
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

        // (a) 必要な場合のみ背景を白クリア
        if (clear_bg) {
            OSData::frame->fillRect(d.x, d.y, d.w, d.h, PICO_BACKGROUND);
        }

        // (b) start_idx から上へ Widget を重ね描きする
        isDirtyDeactivates = true;
        for (size_t i = start_idx; i < hit.size(); ++i) {
            Rect clip = hit[i]->clippedScreenRect().intersection(d);
            if (clip.w <= 0 || clip.h <= 0) continue;
            OSData::frame->setClipRect(clip.x, clip.y, clip.w, clip.h);
            if (hit[i]->getRenderMode() == WidgetTools::OPAQUE) {
                OSData::frame->fillRect(clip.x, clip.y, clip.w, clip.h, hit[i]->getBackgroundColor());
            }
            render_clip = clip;
            render_clip_active = true;
            hit[i]->renderForce();
            render_clip_active = false;
            OSData::frame->clearClipRect();
        }
        isDirtyDeactivates = false;
    }
    draw_frame_total_ms = millis() - buf_timer_ms;
    buf_timer_ms = millis();

    // ★ 2. 液晶へ転送。描き終えたframeの行が、前に送った内容と同じ行は送らない
    //       (液晶の中身は常にframeと同じ、という前提。PushChangedRows()参照)
    BeginRowCompare();
    for (int dirty_i = 0; dirty_i < dirtyRectCount; dirty_i++) {
        perf_pushed_px += PushChangedRows(dirtyRects[dirty_i], dirtyForceBelow[dirty_i]);
    }
    EndRowCompare();

    if(enableDirectRender){
        OSData::lcd->setClipRect(
            directRenderRect.x, directRenderRect.y,
            directRenderRect.w, directRenderRect.h
        );
        OSData::frame->pushSprite(OSData::lcd, 0, 0);
        OSData::lcd->clearClipRect();
        InvalidateLcdRows(directRenderRect.y, directRenderRect.h);
    }
    push_frame_total_ms = millis() - buf_timer_ms;

#if defined(PICOOS_PC)
    // PCビルドだけ: PICOOS_VERIFY_LCD=1 のとき、送り終えた液晶の中身がframeと一致するかを全画素比べる
    // (変わっていない行を送らない最適化が古い絵を残していないかの確認用)
    if (getenv("PICOOS_VERIFY_LCD")) VerifyLcdMatchesFrame();
#endif

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
            "fps: %.1f, px/frame: %lu (skipped %lu), dirtyrects average: %.1f, draw: %lums/frame, push: %lums/frame\n",
            avg_fps, (unsigned long)(perf_pushed_px / perf_frame_count), (unsigned long)(perf_skipped_px / perf_frame_count),
            (float)perf_dirtyrects_total / perf_frame_count,
            perf_draw_total_ms / perf_frame_count,
            perf_push_total_ms / perf_frame_count
        );

        perf_report_start_ms = now_ms;
        perf_pushed_px = 0;
        perf_skipped_px = 0;
        perf_draw_total_ms = 0;
        perf_push_total_ms = 0;
        perf_dirtyrects_total = 0;
        perf_frame_count = 0;
    }
    //! DEBUG !

    dirtyRectCount = 0;
    dirtyOverflowed = false;
    dirtyForceBelowAny = false;
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