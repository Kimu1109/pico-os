#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include "storage/SD_Path.hpp"

namespace LogFunctions {
namespace {
    // SDセクタ(512B)の倍数で確保、書き込みバッファとして蓄積
    constexpr size_t LOG_BUF_SIZE = 4096;
    constexpr uint32_t LOG_FLUSH_INTERVAL_MS = 5000; // 定期フラッシュ間隔
    constexpr uint32_t LOG_PREALLOC_SIZE = 64 * 1024UL;

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
        LOG_SYS_WARN("SD未初期化のためログ保存機能はSerial出力のみで動作します");
        return;
    }

    // O_APPENDで開いたままセッション中保持する(open/closeのたびのオーバーヘッド回避)
    s_fileOpen = s_logFile.open(PICO_Path::FILE::SYS_LOG_TXT, O_WRITE | O_CREAT | O_APPEND);
    if (!s_fileOpen) {
        LOG_SYS_FAIL("log.txtのオープンに失敗しました");
        return;
    }

    // 事前確保でフラグメンテーションと都度のFAT拡張コストを避ける
    s_logFile.preAllocate(LOG_PREALLOC_SIZE);
    // preAllocate()はvalidLength(見かけ上のファイルサイズ)を
    // 即座にLOG_PREALLOC_SIZEまで拡張してしまうため、
    // O_APPENDでの書き込み開始位置がオフセット0ではなく末尾(=予約サイズ分)に
    // ズレてしまう。かつその未書き込み領域はゼロクリアされず、
    // SDカード上の残留データがそのまま読めてしまう(実際に発生した事象)。
    // truncate(0)でvalidLengthを0に戻し、予約クラスタは維持したまま
    // 書き込み開始位置を先頭に正す。
    s_logFile.truncate(0);
    s_lastFlushMs = millis();
}

void Log(LogType type, const char* fmt, ...)
{
    char buf[256];

    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (written < 0) return;

    Serial.printf("%s", GetPrefix(type));
    Serial.print(buf);
    Serial.println();

    // Serial出力用bufとは別に、プレフィックス込みの行をバッファに積む
    char line[288];
    int lineLen = snprintf(line, sizeof(line), "[%lu] %s%s",
                            (unsigned long)millis(), GetPrefix(type), buf);
    if (lineLen < 0) return;
    if ((size_t)lineLen >= sizeof(line)) lineLen = sizeof(line) - 1;

    AppendToBuffer(line, (size_t)lineLen);
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