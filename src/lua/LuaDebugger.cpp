#include "lua/LuaDebugger.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace {
    bool g_enabled = false;
    LuaDebugger::Frontend* g_frontend = nullptr;
    LuaDebugger* g_active = nullptr;
    LuaDebugger::ErrorReporter g_reporter = nullptr;
    LuaDebugger::ActivityHook g_activity = nullptr;
    bool g_redraw = false;

    // シリアルのコマンドの列(1行64バイト、8件)。届くのはPadFunctions::Update()の中、
    // 処理するのはLuaEngine(毎フレーム)か、止まっている間のFrontend
    constexpr int kQueueLen = 8;
    constexpr int kLineLen = 64;
    char g_queue[kQueueLen][kLineLen];
    int g_q_head = 0, g_q_count = 0;

    bool PopLine(char* out){
        if(g_q_count == 0) return false;
        memcpy(out, g_queue[g_q_head], kLineLen);
        g_q_head = (g_q_head + 1) % kQueueLen;
        g_q_count--;
        return true;
    }

    const char* Basename(const char* p){
        const char* b = p;
        for(const char* s = p; *s; s++) if(*s == '/') b = s + 1;
        return b;
    }

    // "main.lua:12" を分ける。成功したらtrue
    bool ParseLocation(const char* s, char* file, size_t file_size, int& line){
        while(*s == ' ') s++;
        const char* colon = strrchr(s, ':');
        if(!colon || colon == s) return false;
        const size_t n = (size_t)(colon - s);
        if(n + 1 > file_size) return false;
        memcpy(file, s, n);
        file[n] = '\0';
        char* end = nullptr;
        const long v = strtol(colon + 1, &end, 10);
        while(end && *end == ' ') end++;
        if(!end || *end != '\0' || v <= 0 || v > 1000000) return false;
        line = (int)v;
        return true;
    }

    const char* ReasonName(LuaDebugger::Reason r){
        switch(r){
            case LuaDebugger::Reason::Breakpoint: return "ブレークポイント";
            case LuaDebugger::Reason::Step:       return "ステップ";
            case LuaDebugger::Reason::Pause:      return "一時停止";
            case LuaDebugger::Reason::Error:      return "エラー";
            case LuaDebugger::Reason::Api:        return "pico.breakpoint()";
        }
        return "?";
    }
}

// ---------------- 全体 ----------------

void LuaDebugger::SetGlobalEnabled(bool on){ g_enabled = on; }
bool LuaDebugger::GlobalEnabled(){ return g_enabled; }
void LuaDebugger::SetFrontend(Frontend* f){ g_frontend = f; }
LuaDebugger::Frontend* LuaDebugger::GetFrontend(){ return g_frontend; }
LuaDebugger* LuaDebugger::Active(){ return g_active; }

void LuaDebugger::SetErrorReporter(ErrorReporter r){ g_reporter = r; }
void LuaDebugger::ReportError(const char* app, const char* message, const char* trace){
    if(g_reporter) g_reporter(app, message, trace);
}
void LuaDebugger::SetActivityHook(ActivityHook h){ g_activity = h; }
void LuaDebugger::NotifyActivity(bool in_lua, const char* app){
    if(g_activity) g_activity(in_lua, app);
}

void LuaDebugger::RequestRedraw(){ g_redraw = true; }
bool LuaDebugger::TakeRedrawRequest(){
    const bool r = g_redraw;
    g_redraw = false;
    return r;
}

bool LuaDebugger::FeedSerialLine(const char* line){
    if(!line || strncmp(line, "dbg", 3) != 0 || (line[3] != ' ' && line[3] != '\0')) return false;
    if(g_q_count >= kQueueLen){
        Serial.printf("[dbg] コマンドが多すぎます(捨てました): %s\n", line);
        return true;
    }
    const int tail = (g_q_head + g_q_count) % kQueueLen;
    strncpy(g_queue[tail], line, kLineLen - 1);
    g_queue[tail][kLineLen - 1] = '\0';
    g_q_count++;
    return true;
}

// ---------------- 生成 ----------------

LuaDebugger::LuaDebugger(){
    g_active = this;
}

LuaDebugger::~LuaDebugger(){
    if(g_active == this) g_active = nullptr;
}

// ---------------- ブレークポイント ----------------

bool LuaDebugger::addBreakpoint(const char* file, int line){
    if(!file || !*file || line <= 0) return false;
    for(int i = 0; i < bp_count_; i++){
        if(bps_[i].line == line && strcmp(bps_[i].file.c_str(), file) == 0) return true;
    }
    if(bp_count_ >= kMaxBreakpoints) return false;
    if(bps_[bp_count_].file.assign(file) == false) return false; // 長すぎる名前は別物を指しうるので断る
    bps_[bp_count_].line = line;
    bp_count_++;
    return true;
}

bool LuaDebugger::removeBreakpoint(const char* file, int line){
    for(int i = 0; i < bp_count_; i++){
        if(bps_[i].line == line && strcmp(bps_[i].file.c_str(), file) == 0){
            for(int j = i; j + 1 < bp_count_; j++) bps_[j] = bps_[j + 1];
            bp_count_--;
            return true;
        }
    }
    return false;
}

void LuaDebugger::clearBreakpoints(){ bp_count_ = 0; }

bool LuaDebugger::wantsLineHook() const {
    return bp_count_ > 0 || step_ != Step::None || pause_requested_;
}

bool LuaDebugger::MatchFile(const char* bp_file, const char* source){
    if(!bp_file || !source) return false;
    if(*source == '@' || *source == '=') source++;
    if(strcmp(bp_file, source) == 0) return true;
    //後ろが "/"+bp_file で終わっていれば当たり("main.lua" も "apps/x/main.lua" も書ける)
    const size_t ls = strlen(source), lb = strlen(bp_file);
    if(lb < ls && source[ls - lb - 1] == '/' && strcmp(source + ls - lb, bp_file) == 0) return true;
    return false;
}

// ---------------- 止まる ----------------

int LuaDebugger::StackDepth(lua_State* L){
    lua_Debug ar;
    int d = 0;
    while(d < 1000 && lua_getstack(L, d, &ar)) d++;
    return d;
}

LuaDebugger::Command LuaDebugger::onLine(lua_State* L, lua_Debug* ar){
    if(paused_) return Command::None;

    bool hit = false;
    Reason reason = Reason::Step;
    if(pause_requested_){
        hit = true;
        reason = Reason::Pause;
    }else if(step_ == Step::Into){
        hit = true;
    }else if(step_ == Step::Over || step_ == Step::Out){
        const int d = StackDepth(L);
        hit = (step_ == Step::Over) ? (d <= step_depth_) : (d < step_depth_);
    }

    if(!hit && bp_count_ > 0){
        bool got_source = false;
        for(int i = 0; i < bp_count_; i++){
            if(bps_[i].line != ar->currentline) continue;
            if(!got_source){
                if(!lua_getinfo(L, "S", ar)) break;
                got_source = true;
            }
            if(MatchFile(bps_[i].file.c_str(), ar->source)){
                hit = true;
                reason = Reason::Breakpoint;
                break;
            }
        }
    }
    if(!hit) return Command::None;
    return pause(L, reason, 0, nullptr);
}

LuaDebugger::Command LuaDebugger::pause(lua_State* L, Reason reason, int level_start, const char* message){
    if(paused_) return Command::None;
    pause_requested_ = false;
    step_ = Step::None;

    fillPauseInfo(L, reason, level_start, message);

    Command c = Command::Continue;
    if(g_frontend){
        paused_ = true;
        c = g_frontend->onPause(*this, L, info_);
        paused_ = false;
    }else{
        //止める手段が無い(Webビルド等)。場所だけ知らせて続ける
        Serial.printf("[dbg] %s: %s(画面が無いので止まらずに続けます)\n", ReasonName(reason),
            info_.top >= 0 ? info_.frames[info_.top].where.c_str() : "?");
    }
    if(c == Command::None) c = Command::Continue;
    applyCommand(L, c, level_start);
    return c;
}

void LuaDebugger::applyCommand(lua_State* L, Command c, int level_start){
    //止まった場所の深さ(Cの関数の中=pico.breakpoint()で止まったなら、その分を引く)。
    //行フックの中で数える深さと揃える
    const int depth = StackDepth(L) - level_start;
    switch(c){
        case Command::StepInto: step_ = Step::Into; break;
        case Command::StepOver: step_ = Step::Over; step_depth_ = depth; break;
        case Command::StepOut:  step_ = Step::Out;  step_depth_ = depth; break;
        default: step_ = Step::None; break;
    }
}

void LuaDebugger::fillPauseInfo(lua_State* L, Reason reason, int level_start, const char* message){
    PauseInfo& in = info_;
    in.reason = reason;
    in.message.assign(message ? message : "");
    in.frame_count = 0;
    in.top = -1;
    in.source.clear();
    in.line = -1;
    in.var_count = 0;
    in.vars_frame = -1;

    lua_Debug ar;
    for(int level = level_start; in.frame_count < kMaxFrames && lua_getstack(L, level, &ar); level++){
        if(!lua_getinfo(L, "Sln", &ar)) continue;
        Frame& f = in.frames[in.frame_count];
        f.level = level;
        f.line = ar.currentline;
        f.name.assign(ar.name ? ar.name : (strcmp(ar.what, "main") == 0 ? "(メイン)" : "?"));
        f.where.clear();
        if(ar.currentline >= 0){
            f.where.appendFormat("%s:%d", Basename(ar.short_src), ar.currentline);
            if(in.top < 0){
                in.top = in.frame_count;
                in.line = ar.currentline;
                if(ar.source && ar.source[0] == '@') in.source.assign(ar.source + 1);
            }
        }else{
            f.where.assign("[C]");
        }
        in.frame_count++;
    }
    if(in.top >= 0) loadVars(L, in, in.top);
}

void LuaDebugger::loadVars(lua_State* L, PauseInfo& in, int frame_index){
    in.var_count = 0;
    in.vars_frame = frame_index;
    if(frame_index < 0 || frame_index >= in.frame_count) return;

    lua_Debug ar;
    if(!lua_getstack(L, in.frames[frame_index].level, &ar)) return;

    char buf[PICO_STR_M];
    //ローカル変数("("で始まるのは中間値なので出さない)
    for(int i = 1; in.var_count < kMaxVars; i++){
        const char* name = lua_getlocal(L, &ar, i);
        if(!name) break;
        if(name[0] != '('){
            FormatValue(L, -1, buf, sizeof(buf));
            in.vars[in.var_count].name.assign(name);
            in.vars[in.var_count].value.assign(buf);
            in.var_count++;
        }
        lua_pop(L, 1);
    }
    //上位値(トップレベルのlocalを関数から使っていると、こちらに出る)
    if(lua_getinfo(L, "f", &ar)){
        for(int i = 1; in.var_count < kMaxVars; i++){
            const char* name = lua_getupvalue(L, -1, i);
            if(!name) break;
            if(name[0] && strcmp(name, "_ENV") != 0){
                FormatValue(L, -1, buf, sizeof(buf));
                in.vars[in.var_count].name.assign("^");
                in.vars[in.var_count].name.append(name);
                in.vars[in.var_count].value.assign(buf);
                in.var_count++;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // 関数
    }
}

// ---------------- 調べる道具 ----------------

void LuaDebugger::FormatValue(lua_State* L, int idx, char* out, size_t size){
    if(!out || size == 0) return;
    idx = lua_absindex(L, idx);
    switch(lua_type(L, idx)){
        case LUA_TNIL:     snprintf(out, size, "nil"); break;
        case LUA_TBOOLEAN: snprintf(out, size, "%s", lua_toboolean(L, idx) ? "true" : "false"); break;
        case LUA_TNUMBER:
            if(lua_isinteger(L, idx)) snprintf(out, size, "%lld", (long long)lua_tointeger(L, idx));
            else snprintf(out, size, "%.14g", (double)lua_tonumber(L, idx));
            break;
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, idx, &len);
            //改行・タブは見えるように置き換え、長いものは切る
            size_t o = 0;
            if(o + 1 < size) out[o++] = '"';
            for(size_t i = 0; i < len && o + 4 < size; i++){
                const char c = s[i];
                if(c == '\n'){ out[o++] = '\\'; out[o++] = 'n'; }
                else if(c == '\t'){ out[o++] = '\\'; out[o++] = 't'; }
                else if((unsigned char)c < 0x20){ out[o++] = '?'; }
                else out[o++] = c;
            }
            //UTF-8の途中で切れていたら、その文字ごと落とす
            if(o > 1 && o + 4 >= size){
                size_t k = o;
                while(k > 1 && ((unsigned char)out[k - 1] & 0xC0) == 0x80) k--;
                if(k > 1 && ((unsigned char)out[k - 1] & 0x80)) k--;
                o = k;
                if(o + 3 < size){ out[o++] = '.'; out[o++] = '.'; }
            }else if(o + 1 < size){
                out[o++] = '"';
            }
            out[o] = '\0';
            break;
        }
        case LUA_TTABLE:
            snprintf(out, size, "table(#%lld)", (long long)lua_rawlen(L, idx));
            break;
        case LUA_TFUNCTION:
            snprintf(out, size, "%s", lua_iscfunction(L, idx) ? "function(C)" : "function");
            break;
        default:
            snprintf(out, size, "%s", lua_typename(L, lua_type(L, idx)));
            break;
    }
}

void LuaDebugger::BuildTrace(lua_State* L, int level_start, char* out, size_t size, int max_frames){
    if(!out || size == 0) return;
    out[0] = '\0';
    size_t len = 0;
    lua_Debug ar;
    int shown = 0;
    int level = level_start;
    for(; lua_getstack(L, level, &ar); level++){
        if(shown >= max_frames) break;
        if(!lua_getinfo(L, "Sln", &ar)) continue;
        int n;
        const char* name = ar.name ? ar.name : (strcmp(ar.what, "main") == 0 ? "(メイン)" : "?");
        if(ar.currentline >= 0){
            n = snprintf(out + len, size - len, "%s%s:%d %s\n", "  ", Basename(ar.short_src), ar.currentline, name);
        }else{
            n = snprintf(out + len, size - len, "  [C] %s\n", name);
        }
        if(n < 0 || (size_t)n >= size - len){ len = size - 1; break; }
        len += (size_t)n;
        shown++;
    }
    if(shown >= max_frames && lua_getstack(L, level, &ar) && len + 8 < size){
        len += (size_t)snprintf(out + len, size - len, "  ...\n");
    }
    //最後の改行は取る
    if(len > 0 && out[len - 1] == '\n') out[--len] = '\0';
}

int LuaDebugger::ReadSourceLines(const char* path, int first, int count, FixedString<PICO_STR_L>* out){
    if(!path || !*path || count <= 0 || !OSData::SD_usable) return 0;
    if(first < 1){ count += first - 1; first = 1; }
    if(count <= 0) return 0;
    FsFile f = OSData::SD.open(path, O_RDONLY);
    if(!f) return 0;
    for(int i = 0; i < count; i++) out[i].clear();

    int line = 1;
    int got = 0;
    char chunk[128];
    bool done = false;
    while(!done){
        const int n = f.read((uint8_t*)chunk, sizeof(chunk));
        if(n <= 0) break;
        for(int i = 0; i < n; i++){
            const char c = chunk[i];
            if(c == '\n'){
                if(line >= first) got = line - first + 1;
                line++;
                if(line >= first + count){ done = true; break; }
                continue;
            }
            if(line >= first && c != '\r'){
                FixedString<PICO_STR_L>& dst = out[line - first];
                if(c == '\t') dst.append("  ");
                else dst.append(c);
            }
        }
    }
    //最後の行が改行で終わっていない
    if(!done && line >= first && line < first + count && out[line - first].length() > 0) got = line - first + 1;
    f.close();
    return got;
}

// ---------------- シリアル ----------------

void LuaDebugger::printBreakpoints() const {
    if(bp_count_ == 0){ Serial.printf("[dbg] ブレークポイントはありません\n"); return; }
    for(int i = 0; i < bp_count_; i++){
        Serial.printf("[dbg] %d: %s:%d\n", i + 1, bps_[i].file.c_str(), bps_[i].line);
    }
}

void LuaDebugger::printTrace() const {
    for(int i = 0; i < info_.frame_count; i++){
        Serial.printf("[dbg] #%d %s %s\n", i, info_.frames[i].where.c_str(), info_.frames[i].name.c_str());
    }
}

void LuaDebugger::printVars() const {
    if(info_.var_count == 0) Serial.printf("[dbg] (変数なし)\n");
    for(int i = 0; i < info_.var_count; i++){
        Serial.printf("[dbg]   %s = %s\n", info_.vars[i].name.c_str(), info_.vars[i].value.c_str());
    }
}

LuaDebugger::Command LuaDebugger::pollSerial(lua_State* L, bool paused, bool* hook_changed){
    if(hook_changed) *hook_changed = false;
    char line[kLineLen];
    while(PopLine(line)){
        const char* arg = line + 3;
        while(*arg == ' ') arg++;
        char cmd[16] = {};
        size_t n = 0;
        while(arg[n] && arg[n] != ' ' && n + 1 < sizeof(cmd)){ cmd[n] = arg[n]; n++; }
        const char* rest = arg + n;
        while(*rest == ' ') rest++;

        if(strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0){
            char file[PICO_STR_M];
            int ln = 0;
            if(!ParseLocation(rest, file, sizeof(file), ln)){
                Serial.printf("[dbg] 書き方: dbg b main.lua:12\n");
            }else if(addBreakpoint(file, ln)){
                Serial.printf("[dbg] ブレークポイント: %s:%d\n", file, ln);
                if(hook_changed) *hook_changed = true;
            }else{
                Serial.printf("[dbg] ブレークポイントを置けません(最大%d個)\n", kMaxBreakpoints);
            }
        }else if(strcmp(cmd, "d") == 0 || strcmp(cmd, "delete") == 0){
            if(!*rest){
                clearBreakpoints();
                Serial.printf("[dbg] ブレークポイントを全部外しました\n");
            }else{
                char file[PICO_STR_M];
                int ln = 0;
                if(ParseLocation(rest, file, sizeof(file), ln) && removeBreakpoint(file, ln)){
                    Serial.printf("[dbg] 外しました: %s:%d\n", file, ln);
                }else{
                    Serial.printf("[dbg] そのブレークポイントはありません: %s\n", rest);
                }
            }
            if(hook_changed) *hook_changed = true;
        }else if(strcmp(cmd, "l") == 0 || strcmp(cmd, "list") == 0){
            printBreakpoints();
        }else if(strcmp(cmd, "pause") == 0){
            if(!paused){
                pause_requested_ = true;
                if(hook_changed) *hook_changed = true;
                Serial.printf("[dbg] 次の行で止めます\n");
            }
        }else if(strcmp(cmd, "bt") == 0){
            if(paused) printTrace(); else Serial.printf("[dbg] 止まっていません\n");
        }else if(strcmp(cmd, "locals") == 0 || strcmp(cmd, "v") == 0){
            if(paused){
                int idx = *rest ? atoi(rest) : (info_.vars_frame >= 0 ? info_.vars_frame : info_.top);
                if(L) loadVars(L, info_, idx);
                printVars();
            }else{
                Serial.printf("[dbg] 止まっていません\n");
            }
        }else if(strcmp(cmd, "c") == 0 || strcmp(cmd, "s") == 0 || strcmp(cmd, "n") == 0 ||
                 strcmp(cmd, "o") == 0 || strcmp(cmd, "q") == 0){
            if(!paused){
                Serial.printf("[dbg] 止まっていません\n");
                continue;
            }
            switch(cmd[0]){
                case 'c': return Command::Continue;
                case 's': return Command::StepInto;
                case 'n': return Command::StepOver;
                case 'o': return Command::StepOut;
                default:  return Command::Abort;
            }
        }else{
            Serial.printf("[dbg] コマンド: b ファイル:行 / d [ファイル:行] / l / pause / c s n o q / bt / locals [段]\n");
        }
    }
    return Command::None;
}
