#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/ProgressBar.hpp"
#include "storage/Sd_Walker.hpp"
#include "util/FixedString.hpp"

#include <stdint.h>

// ストレージの使用量。SDの全体/空きと、フォルダの直下にあるもの1つずつの大きさ(フォルダは中身の合計)。
// ファイルアプリの「他」→「容量」からPush()される(今見ているフォルダから始まる)。
//
// - 空きの量(SdFatの freeClusterCount())はFAT32だとFATを全部読むので数秒かかることがある。画面を1回描いてから
//   1度だけ数え、同じ起動の間は覚えておく(kFreeCacheMs)
// - フォルダの中身の合計は、SdWalkerで1フレームに kStepBudgetUs ぶんずつ数える(フレームを止めない)
// - 数え終わったら大きい順に並べる。フォルダを2回タップするとその中へ入る。「戻る」は1つ上へ(始めたフォルダなら閉じる)
class StorageScene : public Scene {
    public:
        static constexpr int kMaxEntries = 48;
        static constexpr uint32_t kStepBudgetUs = 6000;
        static constexpr uint32_t kFreeCacheMs = 60000;

        explicit StorageScene(const char* path);

        const char* getName() const override { return "Storage"; }
        void onEnter() override;
        void onExit() override;
        void onUpdate() override;

        // 1234 → "1.2KB"。KB/MB/GBは1024の倍数
        static void FormatBytes(uint64_t bytes, char* out, size_t cap);

        struct Entry {
            FixedString<PICO_STR_L> name;
            bool is_dir = false;
            uint64_t size = 0;
        };
        int entryCount() const { return this->entry_count; }
        const Entry& entryAt(int i) const { return this->entries[i]; }
        bool finished() const { return this->counted && this->sorted; }
        uint64_t totalBytes() const { return this->total_bytes; }

    private:
        static constexpr int MARGIN = 3;

        FixedString<PICO_PATH_LEN> start_path;
        FixedString<PICO_PATH_LEN> path;

        Entry entries[kMaxEntries];
        int entry_count = 0;
        bool more_entries = false;
        bool listed = false;      // 直下を読んだ
        int counting = -1;        // 中身を数えているフォルダ(entriesの番号)
        bool counted = false;
        bool sorted = false;
        uint64_t total_bytes = 0;
        SdWalker walker;

        bool want_capacity = false;
        int frames_since_enter = 0;

        Button* back_button = nullptr;
        Label<PICO_STR_L>* path_label = nullptr;
        Label<PICO_STR_L>* capacity_label = nullptr;
        ProgressBar* bar = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        ScrollList* list = nullptr;

        void reset(const char* new_path);
        void listDirect();
        void step();
        void refreshList();
        void refreshCapacity();
        void goUp();

        // 同じ起動の間の空き容量の控え
        static inline bool cap_valid = false;
        static inline uint64_t cap_total = 0, cap_free = 0;
        static inline unsigned long cap_at = 0;
};
