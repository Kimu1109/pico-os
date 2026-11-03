#pragma once

#include <SdFat.h>
#include <SPI.h>

#include "consts.hpp"
#include "hal/HwConfig_Functions.hpp"
#include "OS_Data.hpp"
#include "system/Log_Functions.hpp"
#include "util/FixedString.hpp"

namespace PICO_SD
{
    // ピンは Begin() で HwConfig(内蔵フラッシュの設定。hal/HwConfig_Functions)から決める
    inline SPIClassRP2040 SPI_SD(spi1, SD_MISO, SD_CS, SD_SCK, SD_MOSI);
    inline bool spi_running = false;
    // 最初の Setup() を済ませたか(既定へ戻して試すのは起動時の1回だけ。Remount() では試さない)
    inline bool setup_done = false;

    // c のピンと速さでSDを開く。arduino-picoのSPIは動いている間はピンを変えられないので一度止める
    // (SPI1の端子に使えないピンを渡すと止まるが、それは HwConfigCodec::Validate() が先に弾いている)
    inline bool Begin(const HwConfig& c)
    {
        if (spi_running) SPI_SD.end();
        SPI_SD.setRX(c.sd_miso);
        SPI_SD.setSCK(c.sd_sck);
        SPI_SD.setTX(c.sd_mosi);
        SPI_SD.begin();
        spi_running = true;

        // DEDICATED_SPI: SPI1 はSD専用なので、SdFat に選択(CS)とトランザクションを持たせたままにする。
        // SHARED_SPI だと1セクタ読むたびに「複数ブロックの読み始め(CMD18)+止め(CMD12)」を送っていた
        // (SdFat 2.3 の readSector() は readSectors(…,1) で、SHAREDなら毎回止める)。DEDICATEDなら続きのセクタは
        // コマンド無しで流れてくる(曲・動画・ROM・ログ・録音のような続けて読み書きする所が速くなる)。
        // 書き込みも止めずに続ける。止めるのはファイルの close()/sync()/flush()(→ syncDevice())なので、
        // 閉じた後のデータの安全さは変わらない。docs/design/performance.md「SDをSD専用のSPIとして使う」
        SdSpiConfig config(
            c.sd_cs,
            DEDICATED_SPI,
            c.sd_hz,
            &SPI_SD);
        return OSData::SD.begin(config);
    }

    inline bool Setup()
    {
        using HwConfigCodec::Group;
        bool ok = Begin(HwConfigFunctions::active);
        if (!ok && !setup_done && HwConfigFunctions::CanFallBack(Group::Sd))
        {
            LOG_SYS_WARN("保存したSDの設定で開けないので、既定の設定で試します");
            const HwConfig def = HwConfigFunctions::WithDefault(Group::Sd);
            if (Begin(def))
            {
                HwConfigFunctions::MarkFellBack(Group::Sd, def);
                LOG_SYS_WARN("既定の設定でSDが開けたので、こちらで起動します(hwcfg show で確認)");
                ok = true;
            }
            else
            {
                // SDが入っていないだけならどちらでも開けない。保存した値(使う人の意図)を次の Remount() で使う
                LOG_SYS_WARN("既定の設定でも開けないので、保存した設定のままにします");
            }
        }
        setup_done = true;

        if (!ok)
        {
            LOG_SYS_FAIL("SD Setup has failed!");
            OSData::SD_usable = false;
            return false;
        }

        LOG_SYS_OK("SD Setup has succeeded!");
        OSData::SD_usable = true;
        return true;
    }

    // マウントし直す(USBドライブでPCがSDを書き換えた後。SdFatが覚えているFAT・フォルダの中身を捨てる)
    inline bool Remount()
    {
        OSData::SD_usable = false;
        OSData::SD.end();
        return Setup();
    }

    template<size_t N>
    inline bool ReadTextFile(const FixedString<PICO_PATH_LEN>& path, FixedString<N>& content)
    {
        FsFile f = OSData::SD.open(path.c_str(), O_RDONLY);
        if (!f)
        {
            LOG_SYS_FAIL("Couldn't open a file: %s", path.c_str());
            return false;
        }

        while (f.available())
        {
            if(!content.append((char)f.read())){
                content.clear();
                f.close();
                return false;
            }
        }

        f.close();
        return true;
    }

    template<size_t N>
    inline bool ReadTextFileFast(const FixedString<PICO_PATH_LEN>& path, FixedString<N>& content)
    {
        FsFile f = OSData::SD.open(path.c_str(), O_RDONLY);
        if (!f)
        {
            LOG_APP_FAIL("Couldn't open a file: %s", path.c_str());
            return false;
        }

        size_t size = f.size();

        char *buf = (char *)malloc(size + 1);
        if (!buf)
        {
            f.close();
            LOG_APP_WARN("Couldn't allocate memory: %s", path.c_str());
            return false;
        }

        if (f.read(buf, size) != (int)size)
        {
            free(buf);
            f.close();
            LOG_APP_WARN("Couldn't read file: %s", path.c_str());
            return false;
        }

        buf[size] = '\0';
        f.close();

        if(content.assign(buf)){
            free(buf);
            return true;
        }else{
            content.clear();
            free(buf);
            return false;
        }
    }
}