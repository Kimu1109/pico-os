// 曲のタグ(src/sound/Audio_Tags)と索引(src/apps/music/Music_Library)のテスト。
//
// 1. タグ: ID3v2 の文字コード(ISO-8859-1・Shift_JIS の見分け・UTF-16 BOM付き/BE・UTF-8)、曲番号「3/12」、
//    v2.3(大きなアルバムアートを先に置いても飛ばす)・v2.4(syncsafe の大きさ・UTF-8・フレームの非同期化)・v2.2、
//    圧縮したフレームは読まず ID3v1(Shift_JIS・v1.1 の曲番号)で埋める、.mp3 以外は読まない
// 2. 索引: フォルダを見て作る(少しずつ)・アーティスト/アルバムの一覧(名前順・重ならない・タグ無しは空=「不明」)・
//    その曲の並び(曲番号順・アーティストはアルバム→曲番号)・一覧の曲名を埋める・作り直しで大きさと更新日時が同じなら
//    タグを読み直さない・変われば読み直す・消した曲は消える・無くなったフォルダの曲は並べない・途中でやめても前の索引は残る・
//    UTF-8 を途中で切らない
//
// SDは stubs/ のメモリ上のもの(HostSd::dirs_enabled でフォルダの走査も見る)。
#include "sound/Audio_Tags.hpp"
#include "apps/music/Music_Library.hpp"
#include "storage/SD_Path.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <SdFat.h>
#include <cstdio>
#include <cstring>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

// ---- ID3 を組み立てる ----
static std::string Syncsafe(uint32_t v){
    std::string s;
    s += (char)((v >> 21) & 0x7F); s += (char)((v >> 14) & 0x7F); s += (char)((v >> 7) & 0x7F); s += (char)(v & 0x7F);
    return s;
}
static std::string BE32(uint32_t v){
    std::string s;
    s += (char)(v >> 24); s += (char)(v >> 16); s += (char)(v >> 8); s += (char)v;
    return s;
}
// v2.3/v2.4 のフレーム(flags は2バイト目の書式フラグ)
static std::string Frame(int ver, const char* id, const std::string& body, uint8_t fmt = 0){
    std::string f(id, 4);
    f += ver == 4 ? Syncsafe((uint32_t)body.size()) : BE32((uint32_t)body.size());
    f += (char)0; f += (char)fmt;
    return f + body;
}
static std::string Frame22(const char* id, const std::string& body){
    std::string f(id, 3);
    f += (char)(body.size() >> 16); f += (char)(body.size() >> 8); f += (char)body.size();
    return f + body;
}
static std::string Tag(int ver, const std::string& frames, size_t padding = 64){
    std::string t = "ID3";
    t += (char)ver; t += (char)0; t += (char)0;
    t += Syncsafe((uint32_t)(frames.size() + padding));
    return t + frames + std::string(padding, '\0');
}
static std::string Text(uint8_t enc, const std::string& s){ return std::string(1, (char)enc) + s; }
// UTF-8 → UTF-16(BMPだけ)
static std::string Utf16(const char* u8, bool big, bool bom){
    std::string out;
    if(bom){ out += big ? "\xFE\xFF" : "\xFF\xFE"; }
    const unsigned char* p = (const unsigned char*)u8;
    while(*p){
        uint32_t c;
        if(*p < 0x80) c = *p++;
        else if((*p & 0xE0) == 0xC0){ c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else { c = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        if(big){ out += (char)(c >> 8); out += (char)c; } else { out += (char)c; out += (char)(c >> 8); }
    }
    return out;
}
static std::string Id3v1(const std::string& title, const std::string& artist, const std::string& album, uint8_t track){
    std::string t = "TAG";
    auto field = [&](const std::string& s){ std::string f = s.substr(0, 30); f.resize(30, '\0'); t += f; };
    field(title); field(artist); field(album);
    t += std::string(4, '0');                   //年
    std::string comment(30, '\0');
    comment[29] = (char)track;                  //v1.1: 28バイト目が0、29バイト目が曲番号
    t += comment;
    t += (char)0;                               //ジャンル
    return t;
}
// 中身(音のデータの代わり)
static const std::string kBody(2000, '\x55');

static void TestTags(){
    printf("--- タグの文字コード ---\n");
    using namespace AudioTagReader;
    FixedString<PICO_STR_L> s;
    DecodeText((const uint8_t*)"\x00" "Caf\xE9", 5, s);
    check(strcmp(s.c_str(), "Caf\xC3\xA9") == 0, "ISO-8859-1(é)");
    //「テスト」の Shift_JIS(83 65 83 58 83 67)を ISO-8859-1 の印で入れている(日本のソフトによくある)
    DecodeText((const uint8_t*)"\x00\x83\x65\x83\x58\x83\x67", 7, s);
    check(strcmp(s.c_str(), "テスト") == 0, "印が ISO-8859-1 でも中身が Shift_JIS なら Shift_JIS");
    {
        const std::string le = Text(1, Utf16("夜のうた", false, true));
        DecodeText((const uint8_t*)le.data(), le.size(), s);
        check(strcmp(s.c_str(), "夜のうた") == 0, "UTF-16 リトルエンディアン(BOM)");
        const std::string be = Text(1, Utf16("夜のうた", true, true));
        DecodeText((const uint8_t*)be.data(), be.size(), s);
        check(strcmp(s.c_str(), "夜のうた") == 0, "UTF-16 ビッグエンディアン(BOM)");
        const std::string be2 = Text(2, Utf16("ABC", true, false));
        DecodeText((const uint8_t*)be2.data(), be2.size(), s);
        check(strcmp(s.c_str(), "ABC") == 0, "UTF-16BE(印2)");
    }
    {
        const std::string u = Text(3, "  空の色  ");
        DecodeText((const uint8_t*)u.data(), u.size(), s);
        check(strcmp(s.c_str(), "空の色") == 0, "UTF-8 と前後の空白を落とす");
        const std::string multi = Text(3, std::string("一つ目\0二つ目", 20));
        DecodeText((const uint8_t*)multi.data(), multi.size(), s);
        check(strcmp(s.c_str(), "一つ目") == 0, "v2.4 の複数の値は最初の1つ");
    }
    {
        std::string longtitle;
        for(int i = 0; i < 60; i++) longtitle += "あ";
        const std::string u = Text(3, longtitle);
        DecodeText((const uint8_t*)u.data(), u.size(), s);
        const size_t n = strlen(s.c_str());
        check(n > 0 && n < PICO_STR_L && n % 3 == 0, "長い曲名は文字の切れ目で縮める");
    }
    check(ParseTrackNumber("3/12") == 3 && ParseTrackNumber(" 07") == 7 && ParseTrackNumber("x") == 0, "曲番号");

    printf("--- ID3v2 / ID3v1 ---\n");
    AudioTags t;
    {
        //v2.3: 大きなアルバムアート(APIC)を先に置く
        std::string frames = Frame(3, "APIC", std::string(5000, '\x01'));
        frames += Frame(3, "TIT2", Text(1, Utf16("朝のうた", false, true)));
        frames += Frame(3, "TPE1", Text(0, "Band"));
        frames += Frame(3, "TALB", Text(0, "First"));
        frames += Frame(3, "TRCK", Text(0, "2/9"));
        HostSd::files["/music/v23.mp3"] = Tag(3, frames) + kBody;
        check(Read("/music/v23.mp3", t), "v2.3 を読む");
        check(strcmp(t.title.c_str(), "朝のうた") == 0 && strcmp(t.artist.c_str(), "Band") == 0
              && strcmp(t.album.c_str(), "First") == 0 && t.track == 2, "アルバムアートを飛ばして4つとも読む");
    }
    {
        //v2.4: syncsafe の大きさ(128バイト以上)・UTF-8・長さの印つき・非同期化したフレーム
        std::string title(150, 'a');
        std::string frames = Frame(4, "TIT2", Text(3, title));
        frames += Frame(4, "TPE1", BE32(5) + Text(3, "Mei"), 0x01);
        //非同期化: UTF-16 の BOM(FF FE)の FF の後に 00 を挟んだもの
        frames += Frame(4, "TALB", std::string("\x01\xFF\x00\xFE" "A\x00" "B\x00", 8), 0x02);
        HostSd::files["/music/v24.mp3"] = Tag(4, frames) + kBody;
        check(Read("/music/v24.mp3", t), "v2.4 を読む");
        check(strlen(t.title.c_str()) >= 90 && strcmp(t.artist.c_str(), "Mei") == 0, "syncsafe の大きさ(128以上)・長さの印");
        check(strcmp(t.album.c_str(), "AB") == 0, "フレームの非同期化を戻す(UTF-16 の BOM)");
    }
    {
        std::string frames = Frame22("TT2", Text(0, "Old")) + Frame22("TP1", Text(0, "Ancient")) + Frame22("TRK", Text(0, "5"));
        HostSd::files["/music/v22.mp3"] = Tag(2, frames) + kBody;
        check(Read("/music/v22.mp3", t) && strcmp(t.title.c_str(), "Old") == 0 && strcmp(t.artist.c_str(), "Ancient") == 0 && t.track == 5, "v2.2");
    }
    {
        //圧縮したタイトルは読まず、ID3v1(Shift_JIS)で埋める。アーティストは v2 のまま
        std::string frames = Frame(3, "TIT2", Text(0, "zzzz"), 0x80) + Frame(3, "TPE1", Text(0, "V2Artist"));
        HostSd::files["/music/v1.mp3"] = Tag(3, frames) + kBody + Id3v1("\x83\x65\x83\x58\x83\x67", "V1Artist", "V1Album", 4);
        check(Read("/music/v1.mp3", t), "ID3v1 も読む");
        check(strcmp(t.title.c_str(), "テスト") == 0 && strcmp(t.artist.c_str(), "V2Artist") == 0
              && strcmp(t.album.c_str(), "V1Album") == 0 && t.track == 4, "無い項目だけ ID3v1 で埋める(圧縮は読まない)");
    }
    HostSd::files["/music/plain.mp3"] = kBody;
    check(!Read("/music/plain.mp3", t), "タグが無ければ false");
    HostSd::files["/music/x.wav"] = Tag(3, Frame(3, "TIT2", Text(0, "W"))) + kBody;
    check(!Read("/music/x.wav", t), ".mp3 以外は読まない");
    HostSd::files.clear();
}

static std::string Song(const char* title, const char* artist, const char* album, const char* track){
    std::string frames = Frame(3, "TIT2", Text(3, title)) + Frame(3, "TPE1", Text(3, artist))
                       + Frame(3, "TALB", Text(3, album)) + Frame(3, "TRCK", Text(3, track));
    return Tag(3, frames) + kBody;
}

static void BuildIndex(){
    check(MusicLibrary::StartIndexUpdate(), "索引を作り始める");
    int steps = 0;
    while(!MusicLibrary::StepIndexUpdate(0) && steps < 1000) steps++;
    check(!MusicLibrary::IndexUpdating() && steps > 1, "少しずつ進んで終わる");
}

static void TestIndex(){
    printf("--- 索引 ---\n");
    using namespace MusicLibrary;
    HostSd::dirs_enabled = true;
    HostSd::files["/music/root.mp3"] = Song("Root Song", "Solo", "Lone", "1");
    HostSd::files["/music/A/02.mp3"] = Song("Second", "Band", "First", "2");
    HostSd::files["/music/A/01.mp3"] = Song("First Song", "Band", "First", "1");
    HostSd::files["/music/A/10.mp3"] = Song("Tenth", "Band", "Zeta", "1");
    HostSd::files["/music/B/song.mml"] = "#title X\nA c";
    HostSd::files["/music/B/notes.txt"] = "not music";
    HostSd::files["/sys/.keep"] = "";

    const uint32_t rev = IndexRevision();
    BuildIndex();
    check(IndexRevision() != rev && IndexCount() == 5, "曲を全部入れる(音のファイルだけ)");

    FixedString<PICO_STR_M> names[16];
    int n = ListGroups(Group::Artist, names, 16);
    check(n == 3 && names[0].empty() && strcmp(names[1].c_str(), "Band") == 0 && strcmp(names[2].c_str(), "Solo") == 0,
          "アーティストは名前順・重ならない・タグ無しは空(不明)");
    n = ListGroups(Group::Album, names, 16);
    check(n == 4 && strcmp(names[1].c_str(), "First") == 0, "アルバムの一覧");

    FixedString<PICO_STR_M> folders[16];
    const int fc = ScanFolders(folders, 16);
    check(fc == 2 && strcmp(folders[0].c_str(), "A") == 0, "フォルダは名前順");
    Track tr[64];
    n = ListGroupTracks(Group::Album, "First", folders, fc, tr, 64);
    check(n == 2 && strcmp(tr[0].name.c_str(), "01.mp3") == 0 && strcmp(tr[1].name.c_str(), "02.mp3") == 0
          && tr[0].folder == 0 && strcmp(tr[0].title.c_str(), "First Song") == 0, "アルバムの曲は曲番号順・曲名つき");
    n = ListGroupTracks(Group::Artist, "Band", folders, fc, tr, 64);
    check(n == 3 && strcmp(tr[0].name.c_str(), "01.mp3") == 0 && strcmp(tr[2].name.c_str(), "10.mp3") == 0,
          "アーティストの曲はアルバム名→曲番号");
    n = ListGroupTracks(Group::Artist, "Solo", folders, fc, tr, 64);
    check(n == 1 && tr[0].folder == -1, "直下の曲はフォルダ -1");

    n = ScanTracks(folders, fc, -1, tr, 64);
    FillTitles(tr, n, folders, fc);
    int titled = 0;
    for(int i = 0; i < n; i++) if(!tr[i].title.empty()) titled++;
    check(n == 5 && titled == 4, "一覧の曲名をタグで埋める(MMLは空)");

    //作り直し: 大きさと更新日時が同じならタグを読み直さない(中身だけ変えても前の曲名のまま)
    std::string changed = Song("XXXXXXXX", "Band", "First", "1");
    changed.resize(HostSd::files["/music/A/01.mp3"].size(), '\x55');
    HostSd::files["/music/A/01.mp3"] = changed;
    BuildIndex();
    n = ListGroupTracks(Group::Album, "First", folders, fc, tr, 64);
    check(n == 2 && strcmp(tr[0].title.c_str(), "First Song") == 0, "大きさと更新日時が同じなら前の記録を写す");
    HostSd::mtimes["/music/A/01.mp3"] = 0x5A210000u;
    BuildIndex();
    n = ListGroupTracks(Group::Album, "First", folders, fc, tr, 64);
    check(n == 2 && strcmp(tr[0].title.c_str(), "XXXXXXXX") == 0, "更新日時が変われば読み直す");

    //消した曲・無くなったフォルダ
    HostSd::files.erase("/music/root.mp3");
    BuildIndex();
    check(IndexCount() == 4, "消した曲は索引から消える");
    FixedString<PICO_STR_M> only_b[1] = {FixedString<PICO_STR_M>("B")};
    n = ListGroupTracks(Group::Artist, "Band", only_b, 1, tr, 64);
    check(n == 0, "今は無いフォルダの曲は並べない");

    //途中でやめても前の索引はそのまま
    check(StartIndexUpdate(), "もう一度作り始めて");
    StepIndexUpdate(0);
    CancelIndexUpdate();
    check(!IndexUpdating() && IndexCount() == 4 && HostSd::files.count(PICO_Path::FILE::CFG::SYS_MUSIC_INDEX_TMP) == 0,
          "途中でやめても前の索引は残り、作りかけは消える");

    //壊れた索引は無いのと同じ
    HostSd::files[PICO_Path::FILE::CFG::SYS_MUSIC_INDEX] = "garbage";
    check(IndexCount() == 0 && ListGroups(Group::Artist, names, 16) == 0, "壊れた索引は読まない");
    BuildIndex();
    check(IndexCount() == 4, "作り直せば戻る");

    char buf[8];
    CopyUtf8(buf, sizeof(buf), "ああああ");     //1文字3バイト
    check(strcmp(buf, "ああ") == 0, "UTF-8 を途中で切らない");
    HostSd::dirs_enabled = false;
    HostSd::files.clear();
    HostSd::mtimes.clear();
}

int main(){
    OSData::SD_usable = true;
    TestTags();
    TestIndex();
    printf("\n%s (%d件の失敗)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
