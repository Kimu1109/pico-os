#include "storage/SD_IO.hpp"

#include "OS_Data.hpp"
#include "util/FixedString.hpp"

bool PICO_IO::removeRecursive(const char* path){
    FsFile dir = OSData::SD.open(path);

    if (!dir) {
        return false;
    }

    if (!dir.isDir()) {
        dir.close();
        return OSData::SD.remove(path);
    }

    FsFile entry;

    while (entry.openNext(&dir, O_RDONLY)) {
        FixedString<256> entryName;
        char nameBuf[256];
        entry.getName(nameBuf, sizeof(nameBuf));
        entryName.assign(nameBuf);

        FixedString<256> fullPath;
        // "path/entryName" を組み立てる。PICO_IO::join同様の連結だが、
        // ここではSDライブラリの都合上単純結合で十分(先頭'/'の重複はSD側が許容)。
        fullPath.assign(path);
        fullPath.append("/");
        fullPath.append(entryName);

        if (entry.isDir()) {
            entry.close();

            if (!removeRecursive(fullPath.c_str())) {
                dir.close();
                return false;
            }
        } else {
            entry.close();

            if (!OSData::SD.remove(fullPath.c_str())) {
                dir.close();
                return false;
            }
        }
    }

    dir.close();

    return OSData::SD.rmdir(path);
}
