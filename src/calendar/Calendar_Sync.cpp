#include "calendar/Calendar_Sync.hpp"

#include "OS_Data.hpp"
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "storage/SD_IO.hpp"
#include "storage/SD_Path.hpp"
#include "util/Url.hpp"

#include <cstdio>
#include <cstring>
#include <strings.h>

namespace {
    // sources.cfg の1行を読み、書式が正しければ name/url を返す
    bool ParseSourceLine(const char* line, CalendarSync::Source& out){
        char key[CalendarSync::kMaxNameLen + 1];
        char value[PICO_STR_256B];
        if(PICO_Config::ParseLine(line, key, sizeof(key), value, sizeof(value))
           != PICO_Config::ConfigLineResult::Entry){
            return false;
        }
        if(!CalendarSync::IsValidName(key) || value[0] == '\0') return false;

        out.name.assign(key);
        //Apple/Googleが配る webcal:// は https:// のこと
        if(strncasecmp(value, "webcal://", 9) == 0){
            out.url.assign("https://");
            return out.url.append(value + 9);
        }
        return out.url.assign(value);
    }

    // 読んだ先頭がiCalendarか(ログイン画面のHTML等で手元の .ics を上書きしないため)
    bool LooksLikeIcal(const char* head, size_t len){
        size_t i = 0;
        if(len >= 3 && (uint8_t)head[0] == 0xEF && (uint8_t)head[1] == 0xBB && (uint8_t)head[2] == 0xBF) i = 3;
        while(i < len && (head[i] == ' ' || head[i] == '\t' || head[i] == '\r' || head[i] == '\n')) i++;
        static const char kMagic[] = "BEGIN:VCALENDAR";
        const size_t n = sizeof(kMagic) - 1;
        return len - i >= n && strncasecmp(head + i, kMagic, n) == 0;
    }
}

bool CalendarSync::IsValidName(const char* name){
    if(!name || name[0] == '\0') return false;
    int n = 0;
    for(const char* p = name; *p; p++, n++){
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if(!ok) return false;
    }
    return n <= kMaxNameLen;
}

bool CalendarSync::ReadSource(int index, Source& out){
    if(!OSData::SD_usable || index < 0) return false;

    FsFile f = OSData::SD.open(PICO_Path::FILE::CALENDAR_SOURCES, O_RDONLY);
    if(!f) return false;

    char line[PICO_STR_512B];
    bool tail_of_long_line = false;
    int seen = 0;
    bool found = false;

    int n = 0;
    while((n = f.fgets(line, sizeof(line))) > 0){
        //収まらなかった行は続きごと捨てる(途中で切れたURLは別物を指す)
        const bool complete = (line[n - 1] == '\n');
        if(tail_of_long_line){
            tail_of_long_line = !complete;
            continue;
        }
        if(!complete && n == (int)sizeof(line) - 1){
            tail_of_long_line = true;
            continue;
        }

        Source src;
        if(!ParseSourceLine(line, src)) continue;
        if(seen == index){
            out = src;
            found = true;
            break;
        }
        seen++;
    }
    f.close();
    return found;
}

int CalendarSync::CountSources(){
    int n = 0;
    Source src;
    while(n < kMaxSources && ReadSource(n, src)) n++;
    return n;
}

bool CalendarSync::FileSink::write(const void* data, size_t len){
    if(!file || failed) return false;
    //上限を超えるものは受け取らない(手元の .ics はそのまま残る)
    if(written + len > kMaxIcsBytes){
        failed = true;
        return false;
    }
    if(head_len < sizeof(head) - 1){
        size_t take = sizeof(head) - 1 - head_len;
        if(take > len) take = len;
        memcpy(head + head_len, data, take);
        head_len += take;
        head[head_len] = '\0';
    }
    if(file->write(data, len) != len){
        failed = true;
        return false;
    }
    written += (uint32_t)len;
    return true;
}

bool CalendarSync::begin(){
    this->cancel();

    total_ = CountSources();
    index_ = 0;
    updated_ = 0;
    failed_ = 0;
    last_error_.clear();
    if(total_ == 0){
        state_ = State::Idle;
        return false;
    }

    state_ = State::Running;
    this->startCurrent();
    return true;
}

void CalendarSync::startCurrent(){
    active_ = false;

    Source src;
    if(!ReadSource(index_, src)){
        name_.assign("?");
        this->failCurrent("取得元を読めない");
        return;
    }
    name_.assign(src.name);

    //置き場所: /calendar/<名前>.ics(本体) / .ics.part(書きかけ) / .etag(検証子)
    final_path.assign(PICO_Path::DIR::CALENDAR);
    final_path.append(src.name);
    part_path.assign(final_path);
    etag_path.assign(final_path);
    final_path.append(".ics");
    part_path.append(".ics.part");   // *.ics ではないので CalendarScene は読まない
    etag_path.append(".etag");

    Url url;
    if(!UrlTools::Parse(url, src.url.c_str())){
        this->failCurrent("URLを解釈できない(長すぎるか http(s):// で始まっていない)");
        return;
    }

    //手元に本体があるときだけ検証子を送る(本体が無いのに304を返されても困る)
    FixedString<PICO_STR_M> validator;
    if(OSData::SD.exists(final_path.c_str())){
        FsFile ef = OSData::SD.open(etag_path.c_str(), O_RDONLY);
        if(ef){
            char buf[PICO_STR_M];
            const int n = ef.fgets(buf, sizeof(buf));
            ef.close();
            if(n > 0){
                buf[strcspn(buf, "\r\n")] = '\0';
                validator.assign(buf);
            }
        }
    }

    OSData::SD.mkdir(PICO_Path::DIR::CALENDAR);
    part = OSData::SD.open(part_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if(!part){
        this->failCurrent("書き込み先を用意できない");
        return;
    }
    sink.reset(&part);

    if(!http.begin(url, &sink, validator.empty() ? nullptr : validator.c_str())){
        this->failCurrent(http.failureToStr());
        return;
    }
    active_ = true;
    LOG_APP_MSG("カレンダー: %s を取得します", name_.c_str());
}

void CalendarSync::discardPart(){
    if(part.isOpen()) part.close();
    sink.reset(nullptr);
    if(!part_path.empty() && OSData::SD.exists(part_path.c_str())){
        OSData::SD.remove(part_path.c_str());
    }
}

void CalendarSync::failCurrent(const char* why){
    active_ = false;
    this->discardPart();
    failed_++;

    last_error_.assign(name_);
    last_error_.append(": ");
    last_error_.append(why ? why : "");
    LOG_APP_WARN("カレンダー: %s", last_error_.c_str());
}

void CalendarSync::finishCurrent(){
    active_ = false;

    if(http.getStatus() == TaskTools::FAILED){
        const int code = http.response().statusCode();
        if(sink.failed){
            this->failCurrent("大きすぎる(1MB超)か、書き込みに失敗した");
        }else if(code >= 400){
            char buf[48];
            snprintf(buf, sizeof(buf), "サーバが %d を返しました", code);
            this->failCurrent(buf);
        }else{
            this->failCurrent(http.failureToStr());
        }
        return;
    }

    if(http.isNotModified()){
        this->discardPart();
        LOG_APP_MSG("カレンダー: %s は変更なし", name_.c_str());
        return;
    }

    part.close();
    if(sink.failed){
        this->failCurrent("大きすぎる(1MB超)か、書き込みに失敗した");
        return;
    }
    if(!LooksLikeIcal(sink.head, sink.head_len)){
        this->failCurrent("iCalendarではない応答(URLを確かめてください)");
        return;
    }

    //差し替え。ここまで来て初めて手元の .ics に触る
    if(OSData::SD.exists(final_path.c_str()) && !OSData::SD.remove(final_path.c_str())){
        this->failCurrent("古いファイルを消せない");
        return;
    }
    if(!OSData::SD.rename(part_path.c_str(), final_path.c_str())){
        this->failCurrent("保存に失敗した");
        return;
    }

    const FixedString<PICO_STR_M>& v = http.response().validator();
    if(!v.empty()){
        FsFile ef = OSData::SD.open(etag_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if(ef){
            ef.write(v.c_str(), v.length());
            ef.write("\n", 1);
            ef.close();
        }
    }else if(OSData::SD.exists(etag_path.c_str())){
        //検証子をくれなくなったサーバへ古い検証子を送り続けないように
        OSData::SD.remove(etag_path.c_str());
    }

    updated_++;
    LOG_APP_OK("カレンダー: %s を更新しました (%u B)", name_.c_str(), (unsigned)sink.written);
}

void CalendarSync::next(){
    index_++;
    if(index_ >= total_){
        state_ = State::Done;
        return;
    }
    this->startCurrent();
}

void CalendarSync::update(){
    if(state_ != State::Running) return;

    //開始でつまずいた件(URLが不正等)は、次のフレームで次の件へ進む
    if(!active_){
        this->next();
        return;
    }

    http.update();
    if(http.getStatus() == TaskTools::PROCESSING) return;

    this->finishCurrent();
    this->next();
}

void CalendarSync::cancel(){
    http.cancel();
    active_ = false;
    this->discardPart();
    if(state_ == State::Running) state_ = State::Idle;
}
