#pragma once
// Luaアプリが読み込んだ・作った画像の置き場(pico.image_load / image_create / image_rotate、pico.iso の面の絵)。
// LuaEngine が1つ持ち、LuaEngine_Draw.cpp / _Ext.cpp / _Iso.cpp が images() で使う。
//
// ハンドルは WidgetId と同じ考え方の「generation(上位24bit)+index+1(下位8bit)」。解放のたびに
// generation を進めるので、解放済みの古いハンドルが同じスロットに入った新しい画像を指さない。
// ピクセルのバッファ(LGFX_Sprite)は Lua のメモリ予算の外の素のヒープなので、枚数と合計バイト数の
// 両方に上限を置く(docs/design/lua-api.md「画像」)。
#include <cstddef>
#include <cstdint>
#include "gui/icons/icon_render.h"

class LuaImages {
public:
    static constexpr size_t kMax = 8;
    static constexpr size_t kMaxBytes = 96 * 1024;

    struct Slot {
        IconRender::PimgSprite sprite;
        bool used = false;
        uint32_t generation = 0;
        size_t bytes = 0;   // 使用中のバイト数(bytesUsed() の増減用に覚えておく)
        // pico.image_rotate が焼いた回転済みのコマ(0なら普通の画像)
        uint16_t rot_frames = 0, rot_cols = 0, rot_cell = 0;
    };

    // 有効なハンドルなら添字を入れて true
    bool resolve(uint32_t handle, size_t& out_index) const {
        const uint32_t index1 = handle & 0xFF;
        if (index1 == 0 || index1 > kMax) return false;
        const size_t index = index1 - 1;
        const Slot& slot = slots_[index];
        if (!slot.used || slot.generation == 0 || slot.generation != (handle >> 8)) return false;
        out_index = index;
        return true;
    }
    // 空いているスロットの添字(無ければ -1)
    int findFree() const {
        for (size_t i = 0; i < kMax; ++i) if (!slots_[i].used) return (int)i;
        return -1;
    }
    size_t bytesUsed() const { return bytes_used_; }
    size_t room() const { return kMaxBytes - bytes_used_; }
    bool fits(size_t bytes) const { return bytes_used_ + bytes <= kMaxBytes; }

    // スプライトを用意し終えたスロットを使用中にしてハンドルを返す
    uint32_t commit(size_t index, size_t bytes) {
        Slot& slot = slots_[index];
        slot.used = true;
        slot.bytes = bytes;
        bytes_used_ += bytes;
        // generation 0 は「一度も使われていない」の予約値なので初回だけ1へ進める
        // (2回目以降は release() で進めてある。WidgetRegistry::Register()と同じ考え方)
        if (slot.generation == 0) slot.generation = 1;
        return handleOf(index);
    }
    // スプライトを捨ててスロットを空ける。generation を進めて古いハンドルを無効にする
    void release(size_t index) {
        Slot& slot = slots_[index];
        slot.sprite.sprite.deleteSprite();
        slot.sprite.usable = false;
        bytes_used_ -= slot.bytes;
        slot.bytes = 0;
        slot.rot_frames = slot.rot_cols = slot.rot_cell = 0;
        slot.used = false;
        slot.generation++;
        if (slot.generation == 0) slot.generation = 1; // 0は予約値なのでwrapしたら1へ飛ばす
    }
    uint32_t handleOf(size_t index) const {
        return (slots_[index].generation << 8) | static_cast<uint32_t>(index + 1);
    }

    Slot& operator[](size_t i) { return slots_[i]; }
    const Slot& operator[](size_t i) const { return slots_[i]; }
    Slot* begin() { return slots_; }
    Slot* end() { return slots_ + kMax; }

private:
    Slot slots_[kMax];
    size_t bytes_used_ = 0;
};
