// カレンダーの取得元(/calendar/sources.cfg)の暗号化保存(CalendarSync::WriteSource/ReadSource
// が util/Secret_Cipher.hpp を通すこと)を検証するテスト。
//
// 長いGoogleカレンダー非公開URLを想定した往復・複数件・同名の上書き・後方互換(平文の
// 既存行がそのまま読める)を確認する。実際の通信(CalendarSync::update()等)はここでは見ない
// (calendar_scene_test/ical_testの担当)。
#include "calendar/Calendar_Sync.hpp"
#include "storage/SD_Path.hpp"
#include "functions/Config_Functions.hpp"
#include <cstdio>
#include <cstring>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    OSData::SD_usable = true;
    const char* path = PICO_Path::FILE::CALENDAR_SOURCES;
    HostSd::files.erase(path);

    // Googleカレンダーの非公開URLを模した長い文字列(189文字)
    const char* url1 =
        "https://calendar.google.com/calendar/ical/abcdefghijklmnopqrstuvwxyz1234567890"
        "%40group.calendar.google.com/private-0123456789abcdef0123456789abcdef0123456789"
        "abcdef0123456789abcdef/basic.ics";

    check(CalendarSync::WriteSource("family", url1), "1件目の書き込みが成功する");
    check(strstr(HostSd::files[path].c_str(), "family=enc1:") != nullptr,
          "暗号化された形(enc1:接頭辞)で保存される");
    check(strstr(HostSd::files[path].c_str(), url1) == nullptr,
          "平文URLがそのままSDに書かれていない");

    CalendarSync::Source out;
    check(CalendarSync::ReadSource(0, out), "1件目を読める");
    check(strcmp(out.name.c_str(), "family") == 0, "名前が一致する");
    check(strcmp(out.url.c_str(), url1) == 0, "復号したURLが元と一致する");

    // 2件目(webcal://)を追記
    check(CalendarSync::WriteSource("work", "webcal://example.com/work.ics"),
          "2件目(webcal)の追記が成功する");
    check(CalendarSync::ReadSource(1, out), "2件目を読める");
    check(strcmp(out.url.c_str(), "https://example.com/work.ics") == 0,
          "webcal://がhttps://に変換される");
    check(CalendarSync::CountSources() == 2, "件数が2になる");

    // 同名の上書き(行が増えず、値だけ変わる)
    check(CalendarSync::WriteSource("family", "https://example.com/updated.ics"),
          "同名の上書きが成功する");
    check(CalendarSync::CountSources() == 2, "上書き後も件数は2のまま");
    check(CalendarSync::ReadSource(0, out), "上書き後の1件目を読める");
    check(strcmp(out.url.c_str(), "https://example.com/updated.ics") == 0,
          "上書きしたURLに変わっている");

    // 後方互換: 母艦で直接書いた平文の行もそのまま読める
    HostSd::files[path] += "legacy=https://legacy.example.com/cal.ics\n";
    check(CalendarSync::CountSources() == 3, "平文の行も数えられる(後方互換)");
    check(CalendarSync::ReadSource(2, out), "平文の行を読める");
    check(strcmp(out.url.c_str(), "https://legacy.example.com/cal.ics") == 0,
          "平文URLがそのまま読める");

    // 不正な名前は拒否する
    check(!CalendarSync::WriteSource("has space", "https://example.com/x.ics"),
          "使えない名前(空白を含む)は書き込みを拒否する");
    check(!CalendarSync::WriteSource("", "https://example.com/x.ics"),
          "空の名前は拒否する");

    printf("\n%s\n", failures == 0 ? "ALL PASSED" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
