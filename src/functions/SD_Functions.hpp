#pragma once

#include <SdFat.h>
#include <SPI.h>

#include "consts.hpp"
#include "OS_Data.hpp"
#include "functions/Log_Functions.hpp"
#include "util/FixedString.hpp"

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