// File_Icon.hpp
// ファイル名の拡張子から、ファイル一覧に出すアイコンを選ぶ。
#pragma once

#include "gui/icons/icons_data.h"
#include <string.h>
#include <strings.h>

namespace FileIcon {

inline bool ExtIn(const char* ext, const char* const* list){
    for(; *list; list++) if(strcasecmp(ext, *list) == 0) return true;
    return false;
}

inline IconID For(const char* name, bool is_dir){
    if(is_dir) return IconID::Folder;
    const char* dot = strrchr(name, '.');
    if(!dot) return IconID::File;
    static const char* const kImage[]   = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".pimg", nullptr};
    static const char* const kVideo[]   = {".pvid", ".mp4", ".avi", ".mov", nullptr};
    static const char* const kAudio[]   = {".wav", ".mml", ".mid", ".mp3", ".ogg", nullptr};
    static const char* const kText[]    = {".txt", ".md", ".markdown", ".log", ".csv", ".cfg", ".ini", ".json", nullptr};
    static const char* const kCode[]    = {".lua", ".c", ".cpp", ".h", ".hpp", ".py", ".js", ".html", nullptr};
    static const char* const kArchive[] = {".zip", ".gz", ".tar", ".7z", nullptr};
    if(ExtIn(dot, kImage))   return IconID::FileImage;
    if(ExtIn(dot, kVideo))   return IconID::FileVideo;
    if(ExtIn(dot, kAudio))   return IconID::FileAudio;
    if(ExtIn(dot, kText))    return IconID::FileText;
    if(ExtIn(dot, kCode))    return IconID::FileCode;
    if(ExtIn(dot, kArchive)) return IconID::FileArchive;
    return IconID::File;
}

} // namespace FileIcon
