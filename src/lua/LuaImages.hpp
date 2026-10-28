#pragma once
// Luaアプリが読み込んだ・作った画像の置き場(pico.image_load / image_create / image_rotate、pico.iso の面の絵)。
// LuaEngine が1つ持ち、LuaEngine_Draw.cpp / _Ext.cpp / _Iso.cpp が images() で使う。
//
// ハンドルは WidgetId と同じ考え方の「generation(上位24bit)+index+1(下位8bit)」。解放のたびに
// generation を進めるので、解放済みの古いハンドルが同じスロットに入った新しい画像を指さない。
// ピクセルのバッファ(LGFX_Sprite)は Lua のメモリ予算の外の素のヒープなので、枚数と合計バイト数の
// 両方に上限を置く(docs/design/lua-api.md「画像」)。
//
// フルカラー(pico.image_load の full_color): JPEG・PNG・8bpp以上のBMPは、4bppの絵とは別に
// 「どのファイルをどう縮めたか」を FullColor に覚えておき、Canvas の render で pico.draw_image したときに
// SDから解きながら液晶へそのままの色で直接描く(PICO_GFX::ArmDirectPaint。絵をメモリに持たない)。
// 覚えておける枚数は kMaxFullColor まで(パスを持つので全スロットぶんは持たない)。
#include <cstddef>
#include <cstdint>
#include "gui/icons/icon_render.h"
#include "gui/icons/Image_Codec.hpp"
#include "gui/core/GFX_Functions.hpp"
#include "gui/widgets/WidgetID.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

class LuaImages {
public:
    static constexpr size_t kMax = 8;
    static constexpr size_t kMaxBytes = 96 * 1024;
    static constexpr size_t kMaxFullColor = 2;

    // フルカラーで直描きする画像の覚え書き(ArmDirectPaint の持ち主 ctx はこれのアドレス)
    struct FullColor {
        bool used = false;
        uint8_t slot = 0;                   // どのスロットの画像か
        FixedString<PICO_PATH_LEN> path;
        ImageCodec::Info info;
        ImageCodec::Options opt;            // 縮め方・半透明を混ぜる色
        int16_t x = 0, y = 0;               // 最後に描いた液晶の位置(画像の左上)
        WidgetId canvas = 0;                // 最後に描いた Canvas(消えていれば直描きを他へ譲る)
    };

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
    ~LuaImages() {
        for (size_t i = 0; i < kMax; ++i) dropFullColor(i);
    }

    // スロット index の画像をフルカラーで描けるようにする(覚えておく場所が無ければ null)
    FullColor* addFullColor(size_t index, const char* path, const ImageCodec::Info& info, const ImageCodec::Options& opt) {
        dropFullColor(index);
        for (FullColor& fc : full_) {
            if (fc.used) continue;
            if (!fc.path.assign(path)) return nullptr;   // 切り詰めたパスでは開けない
            fc.used = true;
            fc.slot = (uint8_t)index;
            fc.info = info;
            fc.opt = opt;
            fc.canvas = 0;
            return &fc;
        }
        return nullptr;
    }
    FullColor* fullColorOf(size_t index) {
        for (FullColor& fc : full_) if (fc.used && fc.slot == index) return &fc;
        return nullptr;
    }
    bool isFullColorOwner(const void* ctx) const {
        for (const FullColor& fc : full_) if (fc.used && &fc == ctx) return true;
        return false;
    }
    // フルカラーをやめる(16色の絵だけにする)。液晶に直描きしていた所は frame の絵へ戻す
    void dropFullColor(size_t index) {
        for (FullColor& fc : full_) {
            if (!fc.used || fc.slot != index) continue;
            PICO_GFX::CancelDirectPaint(&fc);
            fc.used = false;
            fc.path.clear();
        }
    }

    // スプライトを捨ててスロットを空ける。generation を進めて古いハンドルを無効にする
    void release(size_t index) {
        dropFullColor(index);
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
    FullColor full_[kMaxFullColor];
    size_t bytes_used_ = 0;
};
