#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

namespace LogFunctions {
namespace {
    // SDセクタ(512B)の倍数。5秒ごとに書き出すので、起動直後のまとまったログでもこの程度で足りる
    // (溢れたらその場で書き出すだけで、失うものは無い)
    constexpr size_t LOG_BUF_SIZE = 1024;
    constexpr uint32_t LOG_FLUSH_INTERVAL_MS = 5000; // 定期フラッシュ間隔
    // 起動時にlog.txtがこれより大きければlog.old.txtへ回して新しく始める(前回のログを1世代残す)
    constexpr uint32_t LOG_ROTATE_BYTES = 256 * 1024UL;
    // 1行の上限(タイムスタンプ+種別+本文)。スタックに置くのでこれ以上大きくしないこと
    // (コア0のスタックは4KBで、Luaの奥深くから呼ばれることもある)
    constexpr size_t LOG_LINE_BYTES = 320;

    char s_logBuf[LOG_BUF_SIZE];
    size_t s_logLen = 0;
    uint32_t s_lastFlushMs = 0;
    bool s_fileOpen = false;

    FsFile s_logFile;

    // バッファに1行分を追記する。溢れる場合は先に既存分を書き出す
    void AppendToBuffer(const char* line, size_t len)
    {
        if (!s_fileOpen) return;

        // 1行自体がバッファサイズを超えるような異常ケースは切り詰める
        if (len >= LOG_BUF_SIZE) {
            len = LOG_BUF_SIZE - 1;
        }

        if (s_logLen + len + 1 > LOG_BUF_SIZE) {
            Flush();
        }

        memcpy(s_logBuf + s_logLen, line, len);
        s_logLen += len;
        s_logBuf[s_logLen++] = '\n';
    }
}

void Setup()
{
    if (!OSData::SD_usable) {
        LOG_SYS_WARN("Log Setup: To init, SD Init should be finished.");
        return;
    }

    // 前回までのログは残し、追記していく。大きくなりすぎたら1世代だけ残して新しく始める
    // (以前は起動のたびにpreAllocate(64KB)してからtruncate(0)していた。truncateが確保した領域を
    // 返すのでpreAllocateは意味が無く、しかも前回のログ(クラッシュの調査に要る)が毎回消えていた)
    {
        FsFile old = OSData::SD.open(PICO_Path::FILE::SYS_LOG_TXT, O_RDONLY);
        const uint32_t size = old ? (uint32_t)old.fileSize() : 0;
        if (old) old.close();
        if (size > LOG_ROTATE_BYTES) {
            OSData::SD.remove(PICO_Path::FILE::SYS_LOG_OLD_TXT);
            OSData::SD.rename(PICO_Path::FILE::SYS_LOG_TXT, PICO_Path::FILE::SYS_LOG_OLD_TXT);
        }
    }

    // O_APPENDで開いたままセッション中保持する(open/closeのたびのオーバーヘッド回避)
    s_fileOpen = s_logFile.open(PICO_Path::FILE::SYS_LOG_TXT, O_WRITE | O_CREAT | O_APPEND);
    if (!s_fileOpen) {
        LOG_SYS_FAIL("Log Setup: Failed to open log.txt");
        return;
    }
    // 前回の起動との区切り
    static const char kBootMark[] = "----- boot -----";
    AppendToBuffer(kBootMark, sizeof(kBootMark) - 1);
    s_lastFlushMs = millis();

    LOG_SYS_OK("Log Setup has succeeded!");
}

void Log(LogType type, const char* fmt, ...)
{
    // 「[時刻] 種別 本文」を1回だけ組み立てる。シリアルへは時刻を除いた部分を出す
    // (以前は本文256Bと行512Bの2つをスタックに置き、2回整形していた)
    FixedString<LOG_LINE_BYTES> line;
    line.appendFormat("[%lu] ", (unsigned long)millis());
    const size_t body = line.length();
    line.append(GetPrefix(type));

    va_list args;
    va_start(args, fmt);
    line.appendFormatV(fmt, args);
    va_end(args);

    Serial.println(line.c_str() + body);

    AppendToBuffer(line.c_str(), line.length());
}

void Flush()
{
    if (!s_fileOpen || s_logLen == 0) return;

    s_logFile.write(s_logBuf, s_logLen);
    s_logFile.sync(); // closeせずデータ保全(sync)のみ行う
    s_logLen = 0;
    s_lastFlushMs = millis();
}

void Update()
{
    if (!s_fileOpen) return;

    if (millis() - s_lastFlushMs > LOG_FLUSH_INTERVAL_MS) {
        Flush();
    }
}

} // namespace LogFunctions