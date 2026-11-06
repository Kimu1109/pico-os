// 液晶・SDの配線と設定(board/HwConfig.hpp)を内蔵フラッシュの末尾4KBに置く仕組みのテスト。
//
// system/HwConfig_Codec(値の読み書き・検査・置く形との変換)と、hal/HwConfig_Functions の
// Load()/Save()/フォールバックの判断/USBシリアルの "hwcfg ..." を、PCの EepromArea(メモリの中)で確かめる。
#include "board/HwConfig.hpp"
#include "hal/HwConfig_Functions.hpp"
#include "storage/Eeprom_Area.hpp"
#include "system/HwConfig_Codec.hpp"
#include "system/Log_Functions.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

static std::string last_log;
void LogFunctions::Log(LogType, const char* fmt, ...){
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    last_log = buf;
}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

using namespace HwConfigCodec;

static bool validate(const HwConfig& c, std::string* why = nullptr){
    char buf[128];
    const bool ok = Validate(c, buf, sizeof(buf));
    if(why) *why = buf;
    return ok;
}

static void testParse(){
    int32_t v = 0;
    check(ParseValue(Kind::Pin, "17", v) && v == 17, "ピン: 数字");
    check(ParseValue(Kind::Pin, "none", v) && v == -1, "ピン: none は -1");
    check(ParseValue(Kind::Pin, "-1", v) && v == -1, "ピン: -1");
    check(!ParseValue(Kind::Pin, "30", v), "ピン: 30 は無い");
    check(!ParseValue(Kind::Pin, "17x", v), "ピン: 後ろに文字があればだめ");
    check(ParseValue(Kind::Hz, "40M", v) && v == 40000000, "速さ: 40M");
    check(ParseValue(Kind::Hz, "400kHz", v) && v == 400000, "速さ: 400kHz");
    check(ParseValue(Kind::Hz, "27000000", v) && v == 27000000, "速さ: 数字だけ");
    check(!ParseValue(Kind::Hz, "0", v), "速さ: 0 はだめ");
    check(!ParseValue(Kind::Hz, "40G", v), "速さ: 知らない単位");
    check(!ParseValue(Kind::Hz, "999M", v), "速さ: 大きすぎ");
    check(ParseValue(Kind::Tri, "auto", v) && v == -1, "三択: auto");
    check(ParseValue(Kind::Tri, "ON", v) && v == 1, "三択: ON(大文字)");
    check(ParseValue(Kind::Tri, "false", v) && v == 0, "三択: false");
    check(!ParseValue(Kind::Tri, "maybe", v), "三択: 知らない語");
    check(ParseValue(Kind::Offset, "-80", v) && v == -80, "ずれ: 負");
    check(!ParseValue(Kind::Offset, "481", v), "ずれ: 範囲外");
    check(!ParseValue(Kind::Rotation, "8", v), "向き: 8 は無い");

    char out[16];
    FormatValue(Kind::Hz, 75000000, out, sizeof(out));
    check(strcmp(out, "75M") == 0, "書き出し: 75M");
    FormatValue(Kind::Hz, 12345, out, sizeof(out));
    check(strcmp(out, "12345") == 0, "書き出し: 端数のある速さ");
    FormatValue(Kind::Pin, -1, out, sizeof(out));
    check(strcmp(out, "none") == 0, "書き出し: none");
    FormatValue(Kind::Tri, -1, out, sizeof(out));
    check(strcmp(out, "auto") == 0, "書き出し: auto");
}

static void testValidate(){
    const HwConfig def = HwConfigDefault();
    check(validate(def), "既定は使える");

    std::string why;
    HwConfig c = def;
    c.lcd_sck = 17;
    check(!validate(c, &why) && why.find("lcd.sck") != std::string::npos, "SPI0のSCKに使えないピンを弾く");

    c = def;
    c.sd_miso = 16;
    check(!validate(c, &why) && why.find("sd.miso") != std::string::npos, "SPI1のMISOに使えないピンを弾く");

    c = def;
    c.lcd_cs = 23;
    check(!validate(c, &why) && why.find("lcd.cs") != std::string::npos, "無線チップのピンを弾く");

    c = def;
    c.lcd_dc = c.touch_cs;
    check(!validate(c, &why) && why.find("touch.cs") != std::string::npos, "同じピンの重複を弾く");

    c = def;
    c.lcd_rst = -1;
    c.lcd_bl = -1;
    check(validate(c), "RSTとバックライトは省ける");

    c = def;
    c.lcd_cs = -1;
    check(!validate(c), "CSは省けない");

    c = def;
    c.lcd_rotation = 1;
    check(!validate(c, &why) && why.find("lcd.rotation") != std::string::npos, "縦横が入れ替わる向きを弾く");
    c.lcd_rotation = 2;
    check(validate(c), "180度は使える");

    c = def;
    c.lcd_hz = 90000000;
    check(!validate(c), "液晶の速さの上限");
    c = def;
    c.sd_hz = 500000;
    check(!validate(c), "SDの速さの下限");

    c = def;
    c.sd_sck = 14; c.sd_mosi = 15; c.sd_miso = 12; c.sd_cs = 10;
    check(validate(c), "SPI1の別の端子の組は使える");
}

static void testSet(){
    HwConfig c = HwConfigDefault();
    const char* why = nullptr;
    check(Set(c, "lcd.hz", "40M", &why) && c.lcd_hz == 40000000, "Set: lcd.hz");
    check(Set(c, "lcd.invert", "on", &why) && c.lcd_invert == 1, "Set: lcd.invert");
    check(!Set(c, "lcd.nope", "1", &why), "Set: 知らないキー");
    check(!Set(c, "lcd.cs", "none", &why), "Set: 省けないピンに none");
    check(Set(c, "lcd.bl", "none", &why) && c.lcd_bl == -1, "Set: 省けるピンに none");
}

static void testCodec(){
    uint8_t buf[512];
    HwConfig c = HwConfigDefault();
    size_t n = Encode(c, buf, sizeof(buf));
    check(n == kHeaderBytes, "既定だけなら本文は空");

    c.lcd_hz = 40000000;
    c.lcd_offset_y = 80;
    c.sd_cs = 7;
    n = Encode(c, buf, sizeof(buf));
    const std::string body(reinterpret_cast<const char*>(buf + kHeaderBytes), n - kHeaderBytes);
    check(body == "lcd.hz=40M\nlcd.offset_y=80\nsd.cs=7\n", "既定と違うキーだけを書く");

    HwConfig d{};
    check(Decode(buf, n, d) == DecodeResult::Ok, "読み戻せる");
    check(d.lcd_hz == 40000000 && d.lcd_offset_y == 80 && d.sd_cs == 7 && d.lcd_cs == HwConfigDefault().lcd_cs, "読み戻した値が合う");

    uint8_t erased[64];
    memset(erased, 0xFF, sizeof(erased));
    check(Decode(erased, sizeof(erased), d) == DecodeResult::Empty, "消したままは Empty");
    check(d.lcd_hz == HwConfigDefault().lcd_hz, "Empty なら既定");

    uint8_t broken[512];
    memcpy(broken, buf, n);
    broken[kHeaderBytes] ^= 1;
    check(Decode(broken, n, d) == DecodeResult::Broken, "本文が化けたら Broken");
    check(d.lcd_hz == HwConfigDefault().lcd_hz, "Broken なら既定");

    memcpy(broken, buf, n);
    broken[6] = 0xFF; broken[7] = 0x0F;
    check(Decode(broken, n, d) == DecodeResult::Broken, "長さが領域を超えたら Broken");

    memcpy(broken, buf, n);
    broken[4] = 2;
    check(Decode(broken, n, d) == DecodeResult::Broken, "知らない版は Broken");

    // 知らないキーは読み飛ばす(新しいファームが足したキー)
    const char* text = "lcd.future=1\nlcd.cs=5\n";
    const size_t len = strlen(text);
    uint8_t blob[128];
    memcpy(blob, buf, kHeaderBytes);
    memcpy(blob + kHeaderBytes, text, len);
    const uint32_t crc = Crc32(blob + kHeaderBytes, len);
    blob[6] = (uint8_t)len; blob[7] = 0;
    blob[8] = (uint8_t)crc; blob[9] = (uint8_t)(crc >> 8); blob[10] = (uint8_t)(crc >> 16); blob[11] = (uint8_t)(crc >> 24);
    check(Decode(blob, kHeaderBytes + len, d) == DecodeResult::Ok && d.lcd_cs == 5, "知らないキーは読み飛ばす");

    check(Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u, "CRC32の検査値");

    HwConfig a = HwConfigDefault(), b = HwConfigDefault();
    b.sd_cs = 7;
    check(SameGroup(a, b, Group::Lcd) && !SameGroup(a, b, Group::Sd), "SameGroup はその部分だけ比べる");
    CopyGroup(a, b, Group::Sd);
    check(a.sd_cs == 7, "CopyGroup");
}

static void feed(const char* line){ HwConfigFunctions::FeedSerialLine(line); }

static void testFunctions(){
    using namespace HwConfigFunctions;
    EepromArea::Erase();
    Load();
    check(source == Source::Default, "何も無ければ Default");
    check(!CanFallBack(Group::Lcd) && !CanFallBack(Group::Sd), "Default ならフォールバックしない");

    check(!FeedSerialLine("hwcfgx"), "hwcfg で始まるだけの別の語は扱わない");
    check(!FeedSerialLine("pad 1"), "ほかの行は扱わない");

    feed("hwcfg set lcd.hz 40M");
    feed("hwcfg set sd.cs=7");
    check(edit.lcd_hz == 40000000 && edit.sd_cs == 7, "set で編集中の値が変わる");
    check(stored.lcd_hz == HwConfigDefault().lcd_hz, "set だけでは保存しない");
    // 23 は形としては合うので set は通し、組み合わせの検査は save でする
    feed("hwcfg set lcd.cs 23");
    check(edit.lcd_cs == 23, "形の合う値は set で入る");
    feed("hwcfg save");
    check(last_log.find("保存できません") != std::string::npos, "使えない組み合わせは保存しない");
    edit.lcd_cs = HwConfigDefault().lcd_cs;
    feed("hwcfg save");
    check(last_log.find("保存しました") != std::string::npos, "保存できる");

    Load();
    check(source == Source::Stored, "保存した値で起動");
    check(active.lcd_hz == 40000000 && active.sd_cs == 7, "active に保存した値");
    check(CanFallBack(Group::Lcd) && CanFallBack(Group::Sd), "既定と違う部分はフォールバックできる");

    const HwConfig def_sd = WithDefault(Group::Sd);
    check(def_sd.sd_cs == HwConfigDefault().sd_cs && def_sd.lcd_hz == 40000000, "WithDefault はその部分だけ既定");
    MarkFellBack(Group::Sd, def_sd);
    check(sd_fell_back && active.sd_cs == HwConfigDefault().sd_cs && active.lcd_hz == 40000000, "MarkFellBack");
    check(!CanFallBack(Group::Sd), "既定へ戻した後はもう試さない");
    check(stored.sd_cs == 7, "フォールバックしてもフラッシュの値は変えない");

    feed("hwcfg unset sd.cs");
    check(edit.sd_cs == HwConfigDefault().sd_cs, "unset で既定へ");
    feed("hwcfg discard");
    check(edit.sd_cs == 7, "discard で保存した値へ");

    feed("hwcfg show");
    check(!last_log.empty(), "show が何か出す");

    feed("hwcfg reset");
    Load();
    check(source == Source::Default && active.lcd_hz == HwConfigDefault().lcd_hz, "reset で既定へ");

    // 壊れた値
    uint8_t junk[16] = { 'P', 'H', 'W', 'C', 1, 0, 4, 0, 0, 0, 0, 0, 'x', 'x', 'x', 'x' };
    EepromArea::Write(junk, sizeof(junk));
    Load();
    check(source == Source::Broken && active.lcd_cs == HwConfigDefault().lcd_cs, "CRCが合わなければ Broken・既定");

    // 使えない組み合わせ(フラッシュを直接書いた場合)
    HwConfig bad = HwConfigDefault();
    bad.lcd_sck = 17;
    uint8_t buf[512];
    const size_t n = Encode(bad, buf, sizeof(buf));
    EepromArea::Write(buf, n);
    Load();
    check(source == Source::Invalid && active.lcd_sck == HwConfigDefault().lcd_sck, "使えない値なら Invalid・既定");
    check(edit.lcd_sck == 17, "Invalid のときは読めた値を編集の出発点にする");
    check(!CanFallBack(Group::Lcd), "Invalid はフォールバックしない(すでに既定)");

    bool rebooted = false;
    static bool* flag = nullptr;
    flag = &rebooted;
    reboot = [](){ *flag = true; };
    feed("hwcfg reboot");
    check(rebooted, "reboot は差し込み口を呼ぶ");
    reboot = nullptr;
    EepromArea::Erase();
}

int main(){
    testParse();
    testValidate();
    testSet();
    testCodec();
    testFunctions();
    printf("%s\n", failures ? "失敗あり" : "全部通過");
    return failures ? 1 : 0;
}
