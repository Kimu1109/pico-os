#pragma once
#include <cstring>
#include <strings.h>

// SDの音のファイルの種類を拡張子(大小は区別しない)で見分ける。再生の窓口(SoundFunctions::WavPlay)が読み取り係を選ぶ判定と、
// 一覧・ファイルビューワー・Wi-Fi転送が「鳴らせるファイルか」を決める判定をここへ集めた(拡張子の追加が1か所で済むように)。
// .mml は別の経路(曲データ)なので、ここには入れない
namespace AudioFormat {
    inline bool HasExtension(const char* path, const char* ext){
        if(!path || !ext) return false;
        const size_t n = strlen(path), e = strlen(ext);
        return n > e && strcasecmp(path + n - e, ext) == 0;
    }
    inline bool IsWav(const char* path){ return HasExtension(path, ".wav"); }
    inline bool IsMp3(const char* path){ return HasExtension(path, ".mp3"); }
    inline bool IsFlac(const char* path){ return HasExtension(path, ".flac"); }
    // .m4a(MP4コンテナ。中身がALACのものだけ鳴らせる。AACは開くときに理由つきで断る)
    inline bool IsM4a(const char* path){ return HasExtension(path, ".m4a"); }
    // WAVの経路(SoundFunctions::WavPlay)で鳴らすファイル(.wav / .mp3 / .flac / .m4a)か
    inline bool IsPcmFile(const char* path){ return IsWav(path) || IsMp3(path) || IsFlac(path) || IsM4a(path); }
}
