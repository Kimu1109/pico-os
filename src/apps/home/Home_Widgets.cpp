#include "apps/home/Home_Widgets.hpp"

#include "apps/home/Home_Layout.hpp"
#include "apps/home/Home_Widget.hpp"
#include "apps/home/Home_Agenda.hpp"
#include "apps/home/Home_Time.hpp"
#include "apps/todo/Todo_Cache.hpp"
#include "apps/music/Music_Service.hpp"
#include "hal/Sound_Functions.hpp"
#include "services/Alarm_Functions.hpp"
#include "system/Time_Functions.hpp"
#include "gui/core/Font_Functions.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/icons/icon_render.h"
#include "storage/SD_Path.hpp"
#include "OS_Data.hpp"
#include "config/App_Config.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
    using Text = Label<PICO_STR_M>;
    constexpr int kPad = 6;
    const char* const kWeekdays[7] = { "日", "月", "火", "水", "木", "金", "土" };

    void DrawText(FontFn::FontSize size, int color, int x, int y, int max_w, const char* s){
        if(max_w <= 0) return;
        Text::DrawPlain(size, (int8_t)color, x, y, max_w, s);
    }
    int TextW(FontFn::FontSize size, const char* s){ return Text::GetTextWidth(size, s); }
    int LineH(FontFn::FontSize size){ return Text::GetLineHeight(size); }
    void DrawCentered(FontFn::FontSize size, int color, int cx, int y, int max_w, const char* s){
        const int w = TextW(size, s);
        int x = cx - w / 2;
        if(w > max_w) x = cx - max_w / 2;
        DrawText(size, color, x, y, max_w, s);
    }

    bool ClockSet(){ return TimeFunctions::year >= HomeTime::kMinValidYear; }

    // 0時からの分。時計が合っていなければ -1
    int MinuteOfDay(){
        if(!ClockSet()) return -1;
        return TimeFunctions::timeinfo.tm_hour * 60 + TimeFunctions::timeinfo.tm_min;
    }
    uint32_t ClockStamp(){
        const struct tm& t = TimeFunctions::timeinfo;
        return ((uint32_t)TimeFunctions::year * 400u + (uint32_t)t.tm_yday) * 1440u
             + (uint32_t)(t.tm_hour * 60 + t.tm_min) + (ClockSet() ? 0u : 0x80000000u);
    }

    // 次に鳴るアラーム(7日以内)。無ければ false。out は "7:30" / "明 7:30"(明日)/ "金 7:30"
    bool NextAlarm(char* out, size_t cap, uint32_t* key = nullptr){
        if(!ClockSet()) return false;
        const struct tm& t = TimeFunctions::timeinfo;
        const int now = t.tm_hour * 60 + t.tm_min;
        int best = -1, best_min = 0;
        for(int d = 0; d < 8 && best < 0; d++){
            const int wday = (t.tm_wday + d) % 7;
            for(int i = 0; i < AlarmFunctions::kMaxAlarms; i++){
                const AlarmFunctions::Alarm& a = AlarmFunctions::Get(i);
                if(!a.enabled) continue;
                if(a.repeat != AlarmFunctions::Repeat::Once && !AlarmFunctions::RepeatMatches(a.repeat, wday)) continue;
                const int m = a.hour * 60 + a.minute;
                if(d == 0 && m <= now) continue;
                if(best < 0 || m < best_min){ best = d; best_min = m; }
            }
        }
        if(best < 0) return false;
        if(best == 0)      snprintf(out, cap, "%d:%02d", best_min / 60, best_min % 60);
        else if(best == 1) snprintf(out, cap, "明 %d:%02d", best_min / 60, best_min % 60);
        else               snprintf(out, cap, "%s %d:%02d", kWeekdays[(t.tm_wday + best) % 7], best_min / 60, best_min % 60);
        if(key) *key = (uint32_t)(best * 1440 + best_min + 1);
        return true;
    }

    // ================= 時計(デジタル) =================
    class ClockWidget : public HomeWidget {
        public:
            const char* appName() const override { return "時計"; }
            uint32_t stamp(uint8_t, uint8_t) override {
                uint32_t key = 0;
                char buf[24];
                NextAlarm(buf, sizeof(buf), &key);
                return ClockStamp() * 31u + key;
            }
            void draw(const Rect& r, uint8_t w, uint8_t, int) override {
                const struct tm& t = TimeFunctions::timeinfo;
                char hm[8], date[24];
                if(ClockSet()){
                    snprintf(hm, sizeof(hm), "%d:%02d", t.tm_hour, t.tm_min);
                    if(w == 1) snprintf(date, sizeof(date), "%d/%d(%s)", t.tm_mon + 1, t.tm_mday, kWeekdays[t.tm_wday % 7]);
                    else       snprintf(date, sizeof(date), "%d/%d", t.tm_mon + 1, t.tm_mday);
                }else{
                    snprintf(hm, sizeof(hm), "--:--");
                    snprintf(date, sizeof(date), "時計が未設定");
                }
                char alarm[24];
                const bool has_alarm = NextAlarm(alarm, sizeof(alarm));

                if(w == 1){
                    const int cx = r.x + r.w / 2;
                    DrawCentered(FontFn::Big, PICO_BLACK, cx, r.y + 8, r.w - 8, hm);
                    DrawCentered(FontFn::Small, PICO_DARKGREY, cx, r.y + 8 + LineH(FontFn::Big) + 2, r.w - 8, date);
                    return;
                }
                //横長: 左に大きな時刻、右に日付・曜日・次のアラーム(右の欄は約70pxしかないので短く書く)
                const int big_h = LineH(FontFn::Bigger);
                const int ty = r.y + (r.h - big_h) / 2;
                DrawText(FontFn::Bigger, PICO_BLACK, r.x + 7, ty, r.w - 14, hm);
                const int tw = TextW(FontFn::Bigger, hm);
                const int rx = r.x + 7 + tw + 6;
                const int rw = r.x + r.w - 4 - rx;
                const int sh = LineH(FontFn::Small);
                int y = r.y + (r.h - sh * (has_alarm ? 3 : 2)) / 2;
                if(ClockSet()){
                    DrawText(FontFn::Small, PICO_BLACK, rx, y, rw, date);
                    char wd[16];
                    snprintf(wd, sizeof(wd), "%s曜日", kWeekdays[t.tm_wday % 7]);
                    DrawText(FontFn::Small, t.tm_wday == 0 ? PICO_RED : t.tm_wday == 6 ? PICO_BLUE : PICO_DARKGREY,
                             rx, y + sh, rw, wd);
                }else{
                    DrawText(FontFn::Small, PICO_DARKGREY, rx, y, rw, date);
                }
                if(has_alarm){
                    //次のアラーム(ベルの印 + 時刻)
                    IconRender::DrawIcon(IconID::Bell, IconSize::Px16, rx, y + sh * 2, PICO_DARKCYAN);
                    DrawText(FontFn::Small, PICO_DARKCYAN, rx + 17, y + sh * 2, rw - 17, alarm);
                }
            }
    };

    // ================= アナログ時計 =================
    class AnalogWidget : public HomeWidget {
        public:
            const char* appName() const override { return "時計"; }
            uint32_t stamp(uint8_t, uint8_t) override { return ClockStamp(); }
            void draw(const Rect& r, uint8_t w, uint8_t h, int) override {
                const struct tm& t = TimeFunctions::timeinfo;
                const bool big = (h >= 2);
                int cx = r.x + r.w / 2;
                const int cy = r.y + r.h / 2;
                const int short_side = (r.w < r.h) ? r.w : r.h;
                const int rad = short_side / 2 - (big ? 8 : 5);
                if(big && w >= 2){
                    //大: 時計を左寄りにして、右に日付
                    cx = r.x + kPad + 4 + rad;
                    if(ClockSet()){
                        const int rx = cx + rad + 10;
                        const int rw = r.x + r.w - kPad - rx;
                        char buf[24];
                        snprintf(buf, sizeof(buf), "%d月%d日", t.tm_mon + 1, t.tm_mday);
                        DrawText(FontFn::Small, PICO_BLACK, rx, cy - 18, rw, buf);
                        snprintf(buf, sizeof(buf), "%s曜日", kWeekdays[t.tm_wday % 7]);
                        DrawText(FontFn::Small, PICO_DARKGREY, rx, cy + 2, rw, buf);
                    }
                }
                OSData::frame->drawCircle(cx, cy, rad, PICO_BLACK);
                if(big) OSData::frame->drawCircle(cx, cy, rad - 1, PICO_BLACK);
                for(int i = 0; i < 12; i++){
                    const float a = i * 30.0f * 3.14159265f / 180.0f;
                    const float dx = sinf(a), dy = -cosf(a);
                    const int inner = rad - ((i % 3 == 0) ? (big ? 10 : 6) : (big ? 5 : 3));
                    OSData::frame->drawLine(cx + (int)lroundf(dx * inner), cy + (int)lroundf(dy * inner),
                                            cx + (int)lroundf(dx * (rad - 2)), cy + (int)lroundf(dy * (rad - 2)), PICO_BLACK);
                }
                if(!ClockSet()) return;
                const float m = (float)t.tm_min;
                const float hr = (float)(t.tm_hour % 12) + m / 60.0f;
                hand(cx, cy, hr * 30.0f, rad * 55 / 100, big ? 4 : 3, PICO_BLACK);
                hand(cx, cy, m * 6.0f, rad * 85 / 100, big ? 3 : 2, PICO_BLACK);
                OSData::frame->fillCircle(cx, cy, big ? 4 : 3, PICO_RED);
            }
        private:
            //4bppのスプライトでは drawWideLine が使えない(docs/design/gui.md「ウィジェットカタログ」のAnalogClock)ので三角形で描く
            static void hand(int cx, int cy, float deg, int len, int half, int color){
                const float a = deg * 3.14159265f / 180.0f;
                const float dx = sinf(a), dy = -cosf(a);
                const float nx = -dy, ny = dx;
                const int tail = half * 2;
                const int tx = cx + (int)lroundf(dx * len), ty = cy + (int)lroundf(dy * len);
                const int bx = cx - (int)lroundf(dx * tail), by = cy - (int)lroundf(dy * tail);
                const int ox = (int)lroundf(nx * half), oy = (int)lroundf(ny * half);
                OSData::frame->fillTriangle(tx, ty, bx + ox, by + oy, bx - ox, by - oy, color);
            }
    };

    // ================= 音楽 =================
#if PICO_APP_MUSIC
    class MusicWidget : public HomeWidget {
        public:
            enum Button { Prev = 0, Play = 1, Next = 2 };

            const char* appName() const override { return "ミュージック"; }

            uint32_t stamp(uint8_t, uint8_t) override {
                uint32_t s = MusicService::Revision() * 7u;
                s += MusicService::Active() ? 1u : 0u;
                s += MusicService::Paused() ? 2u : 0u;
                s += (uint32_t)MusicService::Current() * 13u;
                //鳴っている間は秒が変わるたびに(時間と進み具合)
                if(MusicService::Active() && !MusicService::Paused()) s += elapsedMs() / 1000u * 101u;
                return s;
            }

            void draw(const Rect& r, uint8_t, uint8_t h, int pressed) override {
                const bool big = h >= 2;
                //曲名は本文の文字(文字の大きさの設定に従う)。大きい枠は元から24px
                const FontFn::FontSize title_font = big ? FontFn::Normal : FontFn::Text;
                const int x = r.x + kPad + 2, wmax = r.w - 2 * kPad - 4;
                int y = r.y + (big ? 12 : 5);

                FixedString<PICO_PATH_LEN> title;
                char sub[96];
                const bool has = MusicService::HasQueue() && MusicService::Current() >= 0;
                if(has){
                    const MusicService::Track* tr = MusicService::TrackAt(MusicService::Current());
                    MusicService::DisplayName(tr ? tr->name.c_str() : "", title);
                    snprintf(sub, sizeof(sub), "%s  %d/%d", MusicService::PlaylistName(),
                             MusicService::Position() + 1, MusicService::Count());
                }else{
                    title.assign("ミュージック");
                    snprintf(sub, sizeof(sub), "タップして曲を選ぶ");
                }
                DrawText(title_font, PICO_BLACK, x, y, wmax, title.c_str());
                y += LineH(title_font) + (big ? 4 : 0);
                DrawText(FontFn::Small, PICO_DARKGREY, x, y, wmax, sub);
                y += LineH(FontFn::Small) + (big ? 10 : 2);

                if(!has) return;

                //進み具合
                const uint32_t pos = elapsedMs(), total = totalMs();
                OSData::frame->drawFastHLine(x, y, wmax, PICO_LIGHTGREY);
                if(total > 0){
                    const uint32_t p = pos > total ? total : pos;
                    OSData::frame->fillRect(x, y - 1, (int)((uint64_t)wmax * p / total), 3, PICO_BLACK);
                }
                if(big){
                    char a[12], b[12];
                    fmtTime(pos, a, sizeof(a));
                    if(total > 0) fmtTime(total, b, sizeof(b)); else snprintf(b, sizeof(b), "--:--");
                    DrawText(FontFn::Small, PICO_DARKGREY, x, y + 5, wmax, a);
                    DrawText(FontFn::Small, PICO_DARKGREY, x + wmax - TextW(FontFn::Small, b), y + 5, wmax, b);
                }

                for(int b = 0; b < 3; b++){
                    const Rect br = buttonRect(r, h, b);
                    const int bcx = br.x + br.w / 2, bcy = br.y + br.h / 2;
                    const int rad = br.h / 2;
                    const bool on = (pressed == b);
                    const bool primary = (b == Play);
                    if(on || primary) OSData::frame->fillCircle(bcx, bcy, rad, on ? PICO_DARKGREY : PICO_BLACK);
                    const int fg = (on || primary) ? PICO_WHITE : PICO_BLACK;
                    const int s = rad / 2 + 1;
                    if(b == Play){
                        if(MusicService::Active() && !MusicService::Paused()){
                            OSData::frame->fillRect(bcx - s + 1, bcy - s, s * 2 / 3, s * 2, fg);
                            OSData::frame->fillRect(bcx + s / 3, bcy - s, s * 2 / 3, s * 2, fg);
                        }else{
                            OSData::frame->fillTriangle(bcx - s / 2, bcy - s, bcx - s / 2, bcy + s, bcx + s, bcy, fg);
                        }
                    }else{
                        const int dir = (b == Next) ? 1 : -1;
                        //二重の三角と縦棒(|◀◀ / ▶▶|)
                        OSData::frame->fillTriangle(bcx - dir * s, bcy - s, bcx - dir * s, bcy + s, bcx, bcy, fg);
                        OSData::frame->fillTriangle(bcx, bcy - s, bcx, bcy + s, bcx + dir * s, bcy, fg);
                        OSData::frame->fillRect(dir > 0 ? bcx + s : bcx - s - 2, bcy - s, 2, s * 2 + 1, fg);
                    }
                }
            }

            int buttonAt(const Rect& r, uint8_t, uint8_t h, int x, int y) override {
                if(!MusicService::HasQueue() || MusicService::Current() < 0) return -1;
                for(int b = 0; b < 3; b++){
                    const Rect br = buttonRect(r, h, b);
                    //指で押しやすいよう少し広めに取る
                    if(x >= br.x - 6 && x < br.x + br.w + 6 && y >= br.y - 4 && y < br.y + br.h + 4) return b;
                }
                return -1;
            }

            void press(int b) override {
                switch(b){
                    case Prev: MusicService::Prev(); break;
                    case Play: MusicService::TogglePlay(); break;
                    case Next: MusicService::Next(); break;
                    default: break;
                }
            }

        private:
            static Rect buttonRect(const Rect& r, uint8_t h, int b){
                const int size = h >= 2 ? 40 : 26;
                const int gap = h >= 2 ? 22 : 16;
                const int total = size * 3 + gap * 2;
                const int x0 = r.x + (r.w - total) / 2;
                const int y0 = r.y + r.h - size - (h >= 2 ? 12 : 4);
                return { (int16_t)(x0 + b * (size + gap)), (int16_t)y0, (int16_t)size, (int16_t)size };
            }

            static uint32_t elapsedMs(){
                if(!MusicService::Active()) return 0;
                if(MusicService::PlayingKind() == MusicService::Kind::Wav) return SoundFunctions::WavPositionMs();
                return SoundFunctions::MusicElapsedMs();
            }
            static uint32_t totalMs(){
                if(!MusicService::Active()) return 0;
                if(MusicService::PlayingKind() == MusicService::Kind::Wav) return SoundFunctions::WavDurationMs();
                return SoundFunctions::MusicTotalMs();
            }
            static void fmtTime(uint32_t ms, char* out, size_t cap){
                const uint32_t s = ms / 1000;
                snprintf(out, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
            }
    };
#endif

#if PICO_APP_CALENDAR || PICO_APP_TODO
    // 予定・TODOの1行の左の欄(今日なら時刻、それ以外は曜日+時刻)
    void DayLabel(int32_t day, int32_t today, int32_t sec, bool all_day, char* out, size_t cap){
        char hm[8] = "";
        if(!all_day && sec >= 0) snprintf(hm, sizeof(hm), "%d:%02d", (int)(sec / 3600), (int)(sec / 60 % 60));
        if(day == today){
            snprintf(out, cap, "%s", all_day ? "終日" : hm);
        }else if(day == today + 1){
            if(all_day) snprintf(out, cap, "明日");
            else        snprintf(out, cap, "明 %s", hm);
        }else{
            const char* wd = kWeekdays[((day % 7) + 7 + 4) % 7]; //1970-01-01は木曜
            if(all_day) snprintf(out, cap, "%s", wd);
            else        snprintf(out, cap, "%s %s", wd, hm);
        }
    }

    // 予定・TODOの1行の高さ(16pxの字。横長の78pxに見出し+3行を入れるため、フォントの行の高さより詰める)
    constexpr int kRowH = 18;

    // 予定・TODOの1行。題名は本文の文字(文字の大きさの設定に従う)、左の欄(時刻・曜日)は幅が狭いので16pxのまま。
    // 大きい字では行を文字に合わせて広げ(枠に入る行は減る)、左の欄を行の真ん中へ寄せる
    struct Row {
        int h = kRowH;
        int label_dy = 0;
        Row(){
            if(!FontFn::IsLargeText()) return;
            this->h = LineH(FontFn::Text) + 2;
            this->label_dy = (this->h - LineH(FontFn::Small)) / 2;
        }
    };

    // 見出し(左上の小さな文字)を描いて、本文の始まりの y を返す
    int Header(const Rect& r, const char* left, const char* right){
        const int y = r.y + 3;
        DrawText(FontFn::Small, PICO_DARKGREY, r.x + kPad + 2, y, r.w - 2 * kPad, left);
        if(right && *right){
            const int w = TextW(FontFn::Small, right);
            DrawText(FontFn::Small, PICO_DARKGREY, r.x + r.w - kPad - 2 - w, y, w + 2, right);
        }
        return y + kRowH;
    }
#endif

    // ================= 予定 =================
#if PICO_APP_CALENDAR
    class AgendaWidget : public HomeWidget {
        public:
            const char* appName() const override { return "カレンダー"; }
            void onHomeEnter() override { HomeAgenda::RefreshIfStale(); }
            void tick() override {
                //日付が変わったら読み直す(ランチャを開いたまま0時を回ったとき)
                const int32_t today = HomeTime::Today();
                if(today != HomeAgenda::Day()) HomeAgenda::RefreshIfStale();
            }
            uint32_t stamp(uint8_t, uint8_t) override {
                return HomeAgenda::Revision() * 1440u + (uint32_t)(MinuteOfDay() + 1);
            }
            void draw(const Rect& r, uint8_t, uint8_t h, int) override {
                int32_t now_sec = 0;
                const int32_t today = HomeTime::Today(&now_sec);
                const int x = r.x + kPad + 2, wmax = r.w - 2 * kPad - 4;
                char head[32];
                if(today >= 0){
                    const struct tm& t = TimeFunctions::timeinfo;
                    snprintf(head, sizeof(head), "%d/%d(%s)", t.tm_mon + 1, t.tm_mday, kWeekdays[t.tm_wday % 7]);
                }else{
                    head[0] = '\0';
                }
                int y = Header(r, "予定", head);
                const Row row;
                const int lh = row.h;
                const int lines = (r.y + r.h - 2 - y) / lh;

                if(today < 0){ DrawText(FontFn::Text, PICO_DARKGREY, x, y, wmax, "時計が合っていません"); return; }
                if(HomeAgenda::NoFiles()){ DrawText(FontFn::Text, PICO_DARKGREY, x, y, wmax, "カレンダーがありません"); return; }

                int shown = 0;
                const int col_w = h >= 2 ? 72 : 60;
                for(int i = 0; i < HomeAgenda::Count() && shown < lines; i++){
                    const HomeAgenda::Entry& e = HomeAgenda::At(i);
                    //今日の時刻つきの予定は、終わったら出さない
                    if(e.day == today && e.start_sec >= 0 && e.end_sec > e.start_sec && e.end_sec <= now_sec) continue;
                    //横長は今日と明日だけ
                    if(h < 2 && e.day > today + 1) break;
                    char label[24];
                    if(e.continued) snprintf(label, sizeof(label), "続き");
                    else DayLabel(e.day, today, e.start_sec, e.start_sec < 0, label, sizeof(label));
                    OSData::frame->fillRect(x, y + 3, 3, lh - 6, e.color);
                    DrawText(FontFn::Small, e.day == today ? PICO_BLACK : PICO_DARKGREY, x + 6, y + row.label_dy, col_w - 6, label);
                    DrawText(FontFn::Text, PICO_BLACK, x + col_w, y, wmax - col_w, e.title.c_str());
                    y += lh;
                    shown++;
                }
                if(shown == 0){
                    DrawText(FontFn::Text, PICO_DARKGREY, x, y, wmax, h >= 2 ? "7日以内に予定はありません" : "今日・明日の予定はありません");
                }
            }
    };
#endif

    // ================= TODO =================
#if PICO_APP_TODO
    class TodoWidget : public HomeWidget {
        public:
            const char* appName() const override { return "TODO"; }
            void onHomeEnter() override {
                TodoCache::EnsureLoaded();
                configured_ = OSData::SD_usable && OSData::SD.exists(PICO_Path::FILE::CFG::SYS_TODOIST_CFG);
            }
            uint32_t stamp(uint8_t, uint8_t) override {
                return TodoCache::Revision() * 1440u + (uint32_t)(MinuteOfDay() + 1) + (configured_ ? 0u : 0x40000000u);
            }
            void draw(const Rect& r, uint8_t, uint8_t h, int) override {
                int32_t now_sec = 0;
                const int32_t today = HomeTime::Today(&now_sec);
                const int x = r.x + kPad + 2, wmax = r.w - 2 * kPad - 4;
                char right[24] = "";
                if(TodoCache::Has()) snprintf(right, sizeof(right), "%d件", TodoCache::Total());
                int y = Header(r, "TODO 今日", right);
                const Row row;
                const int lh = row.h;
                int lines = (r.y + r.h - 2 - y) / lh;

                if(!TodoCache::Has()){
                    DrawText(FontFn::Text, PICO_DARKGREY, x, y, wmax, configured_ ? "まだ取得していません" : "TODOアプリで設定してください");
                    return;
                }
                if(TodoCache::Count() == 0){
                    DrawText(FontFn::Text, PICO_DARKGREEN, x, y, wmax, "今日のタスクはありません");
                    y += lh;
                }
                //大きいときは最後の行に取得した時刻
                if(h >= 2) lines--;
                const int col_w = 64;
                for(int i = 0; i < TodoCache::Count() && i < lines; i++){
                    const TodoCache::Entry& e = TodoCache::At(i);
                    const bool overdue = today >= 0 && Todoist::IsOverdue(e.due, today, now_sec);
                    //優先度の印(Todoistの P1=赤 P2=橙→黄 P3=青)
                    const int pc = e.priority >= 4 ? PICO_RED : e.priority == 3 ? PICO_OLIVE : e.priority == 2 ? PICO_BLUE : PICO_LIGHTGREY;
                    OSData::frame->drawCircle(x + 5, y + lh / 2, 4, pc);
                    char label[24];
                    if(e.due.day < today){
                        //過ぎた期限は日付を赤で
                        int y4, mo, d;
                        Todoist::CivilFromDays(e.due.day, y4, mo, d);
                        snprintf(label, sizeof(label), "%d/%d", mo, d);
                    }
                    else if(e.due.has_time) snprintf(label, sizeof(label), "%d:%02d", (int)(e.due.sec / 3600), (int)(e.due.sec / 60 % 60));
                    else snprintf(label, sizeof(label), "今日");
                    DrawText(FontFn::Small, overdue ? PICO_RED : PICO_DARKGREY, x + 13, y + row.label_dy, col_w - 13, label);
                    DrawText(FontFn::Text, PICO_BLACK, x + col_w, y, wmax - col_w, e.content.c_str());
                    y += lh;
                }
                if(h >= 2 && TodoCache::FetchedEpoch() > 0){
                    const time_t ep = (time_t)TodoCache::FetchedEpoch();
                    struct tm l;
                    localtime_r(&ep, &l);
                    char buf[32];
                    snprintf(buf, sizeof(buf), "取得 %d/%d %d:%02d", l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min);
                    DrawText(FontFn::Small, PICO_LIGHTGREY, x, r.y + r.h - 2 - lh, wmax, buf);
                }
            }
        private:
            bool configured_ = false;
    };
#endif

    ClockWidget clock_widget;
    AnalogWidget analog_widget;
#if PICO_APP_MUSIC
    MusicWidget music_widget;
#endif
#if PICO_APP_CALENDAR
    AgendaWidget agenda_widget;
#endif
#if PICO_APP_TODO
    TodoWidget todo_widget;
#endif
}

void HomeWidgets::Setup(){
    HomeLayout::SetImpl(HomeLayout::Kind::Clock, &clock_widget);
    HomeLayout::SetImpl(HomeLayout::Kind::Analog, &analog_widget);
    //ビルド時に外したアプリ(config/App_Config.hpp)のウィジェットは差さない(選ぶ一覧に出ない。置いてあれば名前だけ出す)
#if PICO_APP_MUSIC
    HomeLayout::SetImpl(HomeLayout::Kind::Music, &music_widget);
#endif
#if PICO_APP_CALENDAR
    HomeLayout::SetImpl(HomeLayout::Kind::Agenda, &agenda_widget);
#endif
#if PICO_APP_TODO
    HomeLayout::SetImpl(HomeLayout::Kind::Todo, &todo_widget);
#endif
    HomeLayout::Load();
}
