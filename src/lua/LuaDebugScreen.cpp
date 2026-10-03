#include "lua/LuaDebugScreen.hpp"
#include "functions/Touch_Functions.hpp"
#include "functions/Pad_Functions.hpp"
#include "functions/KeyInput_Functions.hpp"
#include "functions/CrashDump_Functions.hpp"
#include "functions/Font_Functions.hpp"
#include "OS_Data.hpp"
#include "consts.hpp"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

namespace {
    constexpr int kLineH = 17; // 16pxのフォントの"_"は下へ1pxはみ出すので1px空ける
    constexpr int kTitleH = 18;
    constexpr int kLocY = kTitleH;
    constexpr int kMsgY = kLocY + kLineH;
    constexpr int kSrcY = kMsgY + kLineH;
    constexpr int kSrcLines = 5;
    constexpr int kStackY = kSrcY + kSrcLines * kLineH + 2;
    constexpr int kStackLines = 4;
    constexpr int kVarsY = kStackY + kStackLines * kLineH + 2;
    constexpr int kButtonH = 34;
    constexpr int kButtonY = SCREEN_HEIGHT - kButtonH;
    constexpr int kVarLines = (kButtonY - 2 - kVarsY) / kLineH;
    constexpr int kButtons = 5;

    const char* const kButtonLabels[kButtons] = {"続行", "1行", "次へ", "抜ける", "停止"};
    const LuaDebugger::Command kButtonCommands[kButtons] = {
        LuaDebugger::Command::Continue, LuaDebugger::Command::StepInto, LuaDebugger::Command::StepOver,
        LuaDebugger::Command::StepOut, LuaDebugger::Command::Abort,
    };

    const char* ReasonTitle(LuaDebugger::Reason r){
        switch(r){
            case LuaDebugger::Reason::Breakpoint: return "停止: ブレークポイント";
            case LuaDebugger::Reason::Step:       return "停止: ステップ";
            case LuaDebugger::Reason::Pause:      return "停止: 一時停止";
            case LuaDebugger::Reason::Error:      return "停止: エラー";
            case LuaDebugger::Reason::Api:        return "停止: pico.breakpoint()";
        }
        return "停止";
    }

    // 1行を幅に収めて描く(はみ出しは切る)
    void Text(int x, int y, int w, const char* s, uint16_t fg, uint16_t bg){
        LGFX* lcd = OSData::lcd;
        lcd->fillRect(x, y, w, kLineH, bg);
        lcd->setClipRect(x, y, w, kLineH);
        lcd->setTextColor(fg, bg);
        lcd->drawString(s, x + 1, y);
        lcd->clearClipRect();
    }
}

void LuaDebugScreen::draw(const LuaDebugger::PauseInfo& info, int selected_frame){
    LGFX* lcd = OSData::lcd;
    const int W = SCREEN_WIDTH;
    lcd->startWrite();
    lcd->setFont(FontFn::GetSmall());

    // 見出し
    const uint16_t title_bg = info.reason == LuaDebugger::Reason::Error ? TFT_RED : TFT_NAVY;
    lcd->fillRect(0, 0, W, kTitleH, title_bg);
    Text(0, 1, W, ReasonTitle(info.reason), TFT_WHITE, title_bg);

    // 場所
    char line[PICO_STR_L + 16];
    if(selected_frame >= 0 && selected_frame < info.frame_count){
        const LuaDebugger::Frame& f = info.frames[selected_frame];
        snprintf(line, sizeof(line), "%s  %s", f.where.c_str(), f.name.c_str());
    }else{
        snprintf(line, sizeof(line), "(Luaの場所が分かりません)");
    }
    Text(0, kLocY, W, line, TFT_BLACK, TFT_LIGHTGREY);
    Text(0, kMsgY, W, info.message.c_str(), TFT_RED, TFT_WHITE);

    // ソース
    for(int i = 0; i < kSrcLines; i++){
        const int y = kSrcY + i * kLineH;
        if(i < source_count_){
            const int no = source_first_ + i;
            const bool cur = (no == info.line);
            snprintf(line, sizeof(line), "%3d %s", no, source_[i].c_str());
            Text(0, y, W, line, TFT_BLACK, cur ? TFT_YELLOW : TFT_WHITE);
        }else if(i == 0 && source_count_ == 0){
            Text(0, y, W, "(ソースを読めません)", TFT_DARKGREY, TFT_WHITE);
        }else{
            lcd->fillRect(0, y, W, kLineH, TFT_WHITE);
        }
    }

    // スタック
    lcd->drawFastHLine(0, kStackY - 1, W, TFT_DARKGREY);
    for(int i = 0; i < kStackLines; i++){
        const int y = kStackY + i * kLineH;
        if(i < info.frame_count){
            const LuaDebugger::Frame& f = info.frames[i];
            snprintf(line, sizeof(line), "#%d %s %s", i, f.where.c_str(), f.name.c_str());
            const bool sel = (i == selected_frame);
            Text(0, y, W, line, sel ? TFT_WHITE : TFT_BLACK, sel ? TFT_BLUE : TFT_WHITE);
        }else{
            lcd->fillRect(0, y, W, kLineH, TFT_WHITE);
        }
    }

    // 変数
    lcd->drawFastHLine(0, kVarsY - 1, W, TFT_DARKGREY);
    const int pages = info.var_count > 0 ? (info.var_count + kVarLines - 1) / kVarLines : 1;
    if(var_page_ >= pages) var_page_ = 0;
    for(int i = 0; i < kVarLines; i++){
        const int y = kVarsY + i * kLineH;
        const int idx = var_page_ * kVarLines + i;
        if(idx < info.var_count){
            snprintf(line, sizeof(line), "%s = %s", info.vars[idx].name.c_str(), info.vars[idx].value.c_str());
            Text(0, y, W, line, TFT_BLACK, TFT_WHITE);
        }else if(idx == 0){
            Text(0, y, W, "(変数なし)", TFT_DARKGREY, TFT_WHITE);
        }else{
            lcd->fillRect(0, y, W, kLineH, TFT_WHITE);
        }
    }
    if(pages > 1){
        snprintf(line, sizeof(line), "%d/%d", var_page_ + 1, pages);
        lcd->setTextColor(TFT_DARKGREY, TFT_WHITE);
        lcd->drawRightString(line, W - 2, kVarsY);
    }
    lcd->fillRect(0, kVarsY + kVarLines * kLineH, W, kButtonY - (kVarsY + kVarLines * kLineH), TFT_WHITE);

    // ボタン
    const int bw = W / kButtons;
    for(int i = 0; i < kButtons; i++){
        const int x = i * bw;
        const int w = (i == kButtons - 1) ? W - x : bw;
        const uint16_t bg = (i == kButtons - 1) ? TFT_MAROON : TFT_DARKGREEN;
        lcd->fillRect(x, kButtonY, w, kButtonH, bg);
        lcd->drawRect(x, kButtonY, w, kButtonH, TFT_WHITE);
        lcd->setTextColor(TFT_WHITE, bg);
        lcd->drawCenterString(kButtonLabels[i], x + w / 2, kButtonY + (kButtonH - kLineH) / 2);
    }
    lcd->endWrite();
}

LuaDebugger::Command LuaDebugScreen::onPause(LuaDebugger& dbg, lua_State* L, LuaDebugger::PauseInfo& info){
    if(!OSData::lcd) return LuaDebugger::Command::Continue;

    int selected = info.top;
    var_page_ = 0;
    source_first_ = info.line - kSrcLines / 2;
    if(source_first_ < 1) source_first_ = 1;
    source_count_ = info.line > 0
        ? LuaDebugger::ReadSourceLines(info.source.c_str(), source_first_, kSrcLines, source_)
        : 0;

    Serial.printf("[dbg] %s %s %s\n", ReasonTitle(info.reason),
        selected >= 0 ? info.frames[selected].where.c_str() : "?", info.message.c_str());
    OSData::lcd->fillScreen(TFT_WHITE); //区切りの隙間に止まる前の画面が残らないように
    draw(info, selected);

    //止まったときに押していた指(ボタンで止まった等)は、一度離すまで無視する
    bool armed = !OSData::isTouched;
    LuaDebugger::Command result = LuaDebugger::Command::None;

    while(result == LuaDebugger::Command::None){
        CrashDumpFunctions::Feed();
        PICO_Touch::Update();
        PadFunctions::Update();

        //シリアル(dbg c 等)
        result = dbg.pollSerial(L, true, nullptr);
        if(result != LuaDebugger::Command::None) break;

        //物理キーボード
        KeyInputFunctions::Event ev;
        while(KeyInputFunctions::Pop(ev)){
            if(ev.key == KeyInputFunctions::Key::Enter){ result = LuaDebugger::Command::Continue; break; }
            if(ev.key != KeyInputFunctions::Key::Char) continue;
            switch(ev.cp){
                case 'c': result = LuaDebugger::Command::Continue; break;
                case 's': result = LuaDebugger::Command::StepInto; break;
                case 'n': result = LuaDebugger::Command::StepOver; break;
                case 'o': result = LuaDebugger::Command::StepOut; break;
                case 'q': result = LuaDebugger::Command::Abort; break;
                default: break;
            }
            if(result != LuaDebugger::Command::None) break;
        }
        if(result != LuaDebugger::Command::None) break;

        //コントローラー
        if(PadFunctions::Pressed(PadFunctions::A))     result = LuaDebugger::Command::Continue;
        else if(PadFunctions::Pressed(PadFunctions::Down))  result = LuaDebugger::Command::StepInto;
        else if(PadFunctions::Pressed(PadFunctions::Right)) result = LuaDebugger::Command::StepOver;
        else if(PadFunctions::Pressed(PadFunctions::Up))    result = LuaDebugger::Command::StepOut;
        else if(PadFunctions::Pressed(PadFunctions::Home))  result = LuaDebugger::Command::Abort;
        if(result != LuaDebugger::Command::None) break;

        //タッチ
        if(!OSData::isTouched) armed = true;
        if(armed && OSData::isTouchStart){
            const int x = OSData::touchX, y = OSData::touchY;
            if(y >= kButtonY){
                int i = x / (SCREEN_WIDTH / kButtons);
                if(i >= kButtons) i = kButtons - 1;
                result = kButtonCommands[i];
            }else if(y >= kStackY && y < kStackY + kStackLines * kLineH){
                const int i = (y - kStackY) / kLineH;
                if(i < info.frame_count && i != selected){
                    selected = i;
                    var_page_ = 0;
                    dbg.loadVars(L, info, selected);
                    draw(info, selected);
                }
            }else if(y >= kVarsY && y < kButtonY){
                var_page_++;
                draw(info, selected);
            }
        }
        delay(10);
    }

    //止まる前の画面を戻し、次のフレームで全体を描き直してもらう
    if(OSData::frame) OSData::frame->pushSprite(OSData::lcd, 0, 0);
    LuaDebugger::RequestRedraw();
    //指を置いたまま続けると、下の画面が新しいタッチとして受け取ってしまうので、止まっている間に
    //触れていたことは無かったことにする(次に離して触れ直すまで)
    OSData::isTouchStart = false;
    return result;
}
