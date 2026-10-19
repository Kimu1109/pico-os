#pragma once

#include <cstddef>
#include <cstdint>

// ファームの更新の中身(gzip)を置く場所。実機は内蔵フラッシュのLittleFS(arduino-picoのOTAのローダーが
// 次の起動で読んで本体へ写す)、PC/Webはファイルで真似る(Fw_Store.cpp)。ホストテストはメモリの偽物を使う。
//
// 置き場には3つのファイルしか置かない: 中身(kPayloadName)・ローダーへの指示(otacommand.bin)・
// 控え(kNoteName。更新の後の起動で「vN に更新しました」を出すため)。
class FwStore {
    public:
        static constexpr const char* kPayloadName = "pico_fw.gz";
        static constexpr const char* kNoteName    = "pico_fw.note";

        virtual ~FwStore() = default;

        virtual bool mount() = 0;
        virtual void unmount() = 0;
        // このファームのLittleFSの範囲(更新ファイルの見出しと同じでなければ書けない)
        virtual uint32_t fsStart() const = 0;
        virtual uint32_t fsEnd() const = 0;
        // 中身・指示・控えを全部消す
        virtual void clear() = 0;
        // 今置ける中身の大きさ(clear()の後で呼ぶ)
        virtual uint32_t capacity() = 0;

        virtual bool openWrite() = 0;
        virtual bool write(const uint8_t* p, size_t n) = 0;
        virtual bool closeWrite() = 0;
        virtual bool openRead() = 0;
        // 読めたバイト数(終わりで0、失敗で-1)
        virtual int read(uint8_t* p, size_t n) = 0;
        virtual void closeRead() = 0;

        // 中身を確かめた後: 控え(note、1行のテキスト)とローダーへの指示を書く。
        // 指示を書いた時点で、次の起動のときにローダーが本体を書き換える
        virtual bool commit(const char* note) = 0;
        // 控えを読む(無ければfalse)
        virtual bool readNote(char* out, size_t cap) = 0;
};

// 実機/PCの置き場(Fw_Store.cpp)
FwStore& DefaultFwStore();

#if defined(PICOOS_PC)
namespace FwStorePc {
    // 置き場のディレクトリ(main_pc.cppが決める。nullptrなら置き場は無い扱い)
    extern const char* dir;
}
#endif
