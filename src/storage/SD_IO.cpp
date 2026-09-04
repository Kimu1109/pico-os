#include "storage/SD_IO.hpp"

#include "OS_Data.hpp"

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
        char entryPath[256];

        entry.getName(entryPath, sizeof(entryPath));

        char fullPath[256];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", path, entryPath);

        if (entry.isDir()) {
            entry.close();

            if (!removeRecursive(fullPath)) {
                dir.close();
                return false;
            }
        } else {
            entry.close();

            if (!OSData::SD.remove(fullPath)) {
                dir.close();
                return false;
            }
        }
    }

    dir.close();

    return OSData::SD.rmdir(path);
}