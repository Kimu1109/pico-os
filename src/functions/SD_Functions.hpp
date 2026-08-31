#pragma once

#include <SdFat.h>
#include <SPI.h>

#include "consts.hpp"
#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"

namespace PICO_SD
{
    inline SPIClassRP2040 SPI_SD(spi1, SD_MISO, SD_CS, SD_SCK, SD_MOSI);

    inline bool Setup()
    {
        SPI_SD.begin();

        SdSpiConfig config(
            SD_CS,
            SHARED_SPI,
            SD_SCK_MHZ(SD_MAX_SPEED_MHZ),
            &SPI_SD);

        if (!OSData::SD.begin(config))
        {
            LOG_SYS_FAIL("SD Setup has failed!");
            OSData::SD_usable = false;
            return false;
        }

        LOG_SYS_OK("SD Setup has succeeded!");
        OSData::SD_usable = true;
        return true;
    }

    inline String ReadTextFile(const char *path)
    {
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if (!f)
        {
            LOG_SYS_FAIL("Couldn't open a file: %s", path);
            return "";
        }

        String content;
        content.reserve(f.size());

        while (f.available())
        {
            content += (char)f.read();
        }

        f.close();
        return content;
    }

    inline String ReadTextFileFast(const char *path)
    {
        FsFile f = OSData::SD.open(path, O_RDONLY);
        if (!f)
        {
            LOG_APP_FAIL("Couldn't open a file: %s", path);
            return "";
        }

        size_t size = f.size();

        char *buf = (char *)malloc(size + 1);
        if (!buf)
        {
            f.close();
            LOG_APP_WARN("Couldn't allocate memory: %s", path);
            return "";
        }

        if (f.read(buf, size) != (int)size)
        {
            free(buf);
            f.close();
            LOG_APP_WARN("Couldn't read file: %s", path);
            return "";
        }

        buf[size] = '\0';
        f.close();

        String content(buf);
        free(buf);

        return content;
    }
}