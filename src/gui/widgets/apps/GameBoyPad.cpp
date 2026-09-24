#include "gui/widgets/apps/GameBoyPad.hpp"

#include "gb/Gb_Emu.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"

#include <cstdlib>

// ---- 配置(ローカル座標、幅240・高さ84が前提) ----
// 十字キー: 左の列(x < kMidX0)
static constexpr int kDpadX = 4;
static constexpr int kDpadY = 3;
static constexpr int kDpadSize = 78;
static constexpr int kDpadCell = kDpadSize / 3;
// 中央の列(SELECT / START / ROM・戻る)
static constexpr int kMidX0 = 84;
static constexpr int kMidX1 = 158;
static constexpr int kRowSelectY = 4;
static constexpr int kRowStartY = 30;
static constexpr int kRowCmdY = 56;
static constexpr int kRowH = 22;
static constexpr int kCmdH = 24;
static constexpr int kCmdGap = 2;
static constexpr int kCmdW = (kMidX1 - kMidX0 - kCmdGap) / 2;
// 右の列(A / B)。本物と同じく右上がりに並べる
static constexpr int kBx = 178, kBy = 54;
static constexpr int kAx = 216, kAy = 30;
static constexpr int kAbR = 19;

// 十字キーの中心付近は方向が定まらないので何も押さない
static constexpr int kDpadDeadZone = 6;

GameBoyPad::GameBoyPad(int x, int y, int w){
    this->l_rect = { (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)kPadH };
    this->background_color = PICO_WHITE;
}

GameBoyPad::Area GameBoyPad::areaAt(int px, int py, uint8_t& buttons) const {
    buttons = 0;

    if(px < kMidX0){
        // 十字キー: 中心からの角度で8方向。tan(22.5°)≒0.414 ≒ 5/12 を境に斜めを取る
        const int dx = px - (kDpadX + kDpadSize / 2);
        const int dy = py - (kDpadY + kDpadSize / 2);
        const int ax = abs(dx), ay = abs(dy);
        if(ax < kDpadDeadZone && ay < kDpadDeadZone) return Area::Game;

        const bool horizontal = ay * 12 < ax * 5;
        const bool vertical = ax * 12 < ay * 5;
        if(!vertical) buttons |= (dx < 0) ? GbEmu::Left : GbEmu::Right;
        if(!horizontal) buttons |= (dy < 0) ? GbEmu::Up : GbEmu::Down;
        return Area::Game;
    }

    if(px < kMidX1){
        if(py < kRowStartY - 2){ buttons = GbEmu::Select; return Area::Game; }
        if(py < kRowCmdY - 2){   buttons = GbEmu::Start;  return Area::Game; }
        return (px < kMidX0 + kCmdW + kCmdGap / 2) ? Area::Rom : Area::Back;
    }

    // A / B: 近いほう
    const int da = (px - kAx) * (px - kAx) + (py - kAy) * (py - kAy);
    const int db = (px - kBx) * (px - kBx) + (py - kBy) * (py - kBy);
    buttons = (da <= db) ? GbEmu::A : GbEmu::B;
    return Area::Game;
}

void GameBoyPad::setPressed(uint8_t buttons, bool over){
    if(buttons == this->pressed && over == this->over_command) return;
    this->pressed = buttons;
    this->over_command = over;
    this->needsRender();
}

void GameBoyPad::track(){
    const Rect g = this->getScreenRect();
    uint8_t buttons = 0;
    const Area area = this->areaAt(OSData::touchX - g.x, OSData::touchY - g.y, buttons);

    if(this->press_area == Area::Game){
        // ゲームのボタンは指を滑らせると切り替わる。ROM/戻るの上へ滑らせたら何も押さない
        this->setPressed((area == Area::Game) ? buttons : 0, false);
    }else{
        // ROM/戻るは押し始めたものの上にいる間だけ「押している」
        this->setPressed(0, area == this->press_area);
    }
}

void GameBoyPad::causeOnPressStart(){
    Widget::causeOnPressStart();
    const Rect g = this->getScreenRect();
    uint8_t buttons = 0;
    this->press_area = this->areaAt(OSData::touchX - g.x, OSData::touchY - g.y, buttons);
    this->track();
}

void GameBoyPad::causeOnPressMove(){
    Widget::causeOnPressMove();
    this->track();
}

void GameBoyPad::causeOnPressEnd(){
    Widget::causeOnPressEnd();

    const Area started = this->press_area;
    const bool fire = this->over_command;
    this->press_area = Area::None;
    this->setPressed(0, false);

    if(!fire) return;
    if(started == Area::Rom && this->on_rom) this->on_rom();
    if(started == Area::Back && this->on_back) this->on_back();
}

void GameBoyPad::drawDpad(int ox, int oy){
    const int c = kDpadCell;
    const int x = ox + kDpadX;
    const int y = oy + kDpadY;

    // 十字の形(横棒+縦棒)
    OSData::frame->fillRect(x, y + c, kDpadSize, c, PICO_DARKGREY);
    OSData::frame->fillRect(x + c, y, c, kDpadSize, PICO_DARKGREY);

    // 押している方向だけ黒く
    const uint8_t p = this->pressed;
    if(p & GbEmu::Left)  OSData::frame->fillRect(x,         y + c,     c, c, PICO_BLACK);
    if(p & GbEmu::Right) OSData::frame->fillRect(x + 2 * c, y + c,     c, c, PICO_BLACK);
    if(p & GbEmu::Up)    OSData::frame->fillRect(x + c,     y,         c, c, PICO_BLACK);
    if(p & GbEmu::Down)  OSData::frame->fillRect(x + c,     y + 2 * c, c, c, PICO_BLACK);

    // 真ん中の目印
    OSData::frame->fillCircle(x + c + c / 2, y + c + c / 2, 4, PICO_LIGHTGREY);
}

void GameBoyPad::drawRoundButton(int cx, int cy, const char* label, bool on){
    OSData::frame->fillCircle(cx, cy, kAbR, on ? PICO_BLACK : PICO_MAROON);
    OSData::frame->setTextColor(PICO_WHITE);
    const int tw = OSData::frame->textWidth(label);
    const int th = OSData::frame->fontHeight();
    OSData::frame->setCursor(cx - tw / 2, cy - th / 2);
    OSData::frame->print(label);
}

void GameBoyPad::drawPill(int x, int y, int w, int h, const char* label, bool on){
    if(on) OSData::frame->fillRect(x, y, w, h, PICO_BLACK);
    else   OSData::frame->drawRect(x, y, w, h, PICO_DARKGREY);
    OSData::frame->setTextColor(on ? PICO_WHITE : PICO_BLACK);
    const int tw = OSData::frame->textWidth(label);
    const int th = OSData::frame->fontHeight();
    OSData::frame->setCursor(x + (w - tw) / 2, y + (h - th) / 2);
    OSData::frame->print(label);
}

void GameBoyPad::render(){
    if(!this->needs_redraw) return;
    if(!this->visible) return;

    if(this->prev_l_rect != this->l_rect) markdirty(getScreenPrevRect());
    const Rect g = this->getScreenRect();
    markdirty(g);

    FontFn::SetSmall();

    this->drawDpad(g.x, g.y);

    const int mw = kMidX1 - kMidX0;
    this->drawPill(g.x + kMidX0, g.y + kRowSelectY, mw, kRowH, "SELECT", (this->pressed & GbEmu::Select) != 0);
    this->drawPill(g.x + kMidX0, g.y + kRowStartY,  mw, kRowH, "START",  (this->pressed & GbEmu::Start) != 0);

    const bool rom_on = this->over_command && this->press_area == Area::Rom;
    const bool back_on = this->over_command && this->press_area == Area::Back;
    this->drawPill(g.x + kMidX0, g.y + kRowCmdY, kCmdW, kCmdH, "ROM", rom_on);
    this->drawPill(g.x + kMidX0 + kCmdW + kCmdGap, g.y + kRowCmdY, kCmdW, kCmdH, "戻る", back_on);

    this->drawRoundButton(g.x + kBx, g.y + kBy, "B", (this->pressed & GbEmu::B) != 0);
    this->drawRoundButton(g.x + kAx, g.y + kAy, "A", (this->pressed & GbEmu::A) != 0);

    this->prev_l_rect = this->l_rect;
    this->needs_redraw = false;
}
