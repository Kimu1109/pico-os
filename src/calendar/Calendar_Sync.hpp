#pragma once

#include "task/Http_Get.hpp"
#include "net/Http_Response.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

#include "SdFat.h"

#include <cstdint>

// カレンダーの取得元(iCalのURL)から .ics を取ってきて、SDの /calendar/ へ置く係。
//
// 取得元は /calendar/sources.cfg に1行1件で書く:
//
//     # 名前 = URL
//     family = https://calendar.google.com/calendar/ical/.../private-.../basic.ics
//
// 取ってきたものは /calendar/<名前>.ics に置くので、CalendarScene は今までどおり
// 「/calendar/*.ics を全部読む」だけでよい(ネットワークとの境界がファイルで切れる。
// Doc_Fetch/MarkdownView と同じ考え方)。
//
// Doc_Fetch/Doc_Cache を通さないのは:
//   - Doc_Cacheは1件64KiBで頭打ちにしている(文書向け)。Googleの非公開URLは
//     過去の予定を全部返すので数百KBになる
//   - キャッシュは /cache/<ホスト>/<パス> にミラーするので、非公開URLの秘密の部分が
//     ディレクトリ名としてSDに散らばる
//
// 守っていること:
//   - **取得に失敗しても、手元の <名前>.ics は壊さない。** 一時ファイル(.part)へ書き、
//     200で最後まで読めて中身がiCalendarだったときだけ差し替える(Doc_Cache::Writerと同じ)
//   - サーバが検証子(ETag/Last-Modified)を返すなら <名前>.etag に覚え、次から条件付きGETにする
//   - 名前はファイル名になるので英数字と _ - だけ(FATで困らないように)
//   - `webcal://` はそのまま https:// として扱う(Apple/Googleが配るリンクの形)
//
// 取得は1件ずつ順番に行う。1件ぶんの HttpGet しか持たないので、同時に何本も繋がない。
class CalendarSync {
    public:
        // 取得元の上限と、1件の .ics の上限(SDを埋めないための頭打ち。読むのは流しながら)
        static constexpr int kMaxSources = 8;
        static constexpr uint32_t kMaxIcsBytes = 1024u * 1024u;
        // 名前(=ファイル名)の最大文字数
        static constexpr int kMaxNameLen = 20;

        struct Source {
            FixedString<PICO_STR_S> name;
            FixedString<PICO_STR_256B> url;
        };

        enum class State : uint8_t { Idle, Running, Done };

        CalendarSync() = default;
        ~CalendarSync(){ cancel(); }

        // sources.cfg の index 件目(書式の正しい行だけを数える)を読む
        static bool ReadSource(int index, Source& out);
        // 書式の正しい取得元の数。ファイルが無ければ0
        static int CountSources();
        // 名前がファイル名として使えるか
        static bool IsValidName(const char* name);

        // 全件の取得を始める。取得元が1件も無ければfalse
        bool begin();
        // 毎フレーム呼ぶ。1件終わるごとに次へ進む
        void update();
        // 途中でやめる(書きかけは捨て、手元の .ics はそのまま)
        void cancel();

        State state() const { return state_; }
        int total() const { return total_; }
        int current() const { return index_; }       // 0始まり。いま何件目を取っているか
        int updatedCount() const { return updated_; } // 新しい中身で差し替えた数
        int failedCount() const { return failed_; }
        // 最後に失敗した理由(画面へそのまま出せる日本語)
        const char* lastError() const { return last_error_.c_str(); }

    private:
        // 本文を一時ファイルへ書く。先頭だけ覚えておき、iCalendarかどうかを確かめる
        class FileSink : public IHttpSink {
            public:
                FsFile* file = nullptr;
                uint32_t written = 0;
                bool failed = false;
                char head[32] = {};
                size_t head_len = 0;

                void reset(FsFile* f){ file = f; written = 0; failed = false; head_len = 0; head[0] = '\0'; }
                bool write(const void* data, size_t len) override;
        };

        HttpGet http;
        FsFile part;
        FileSink sink;

        FixedString<PICO_PATH_LEN> final_path;
        FixedString<PICO_PATH_LEN> part_path;
        FixedString<PICO_PATH_LEN> etag_path;
        FixedString<PICO_STR_S> name_;

        State state_ = State::Idle;
        int total_ = 0;
        int index_ = 0;
        int updated_ = 0;
        int failed_ = 0;
        bool active_ = false; // index_ 件目を取得中
        FixedString<PICO_STR_LL> last_error_;

        void startCurrent();
        void finishCurrent();
        void failCurrent(const char* why);
        void discardPart();
        void next();
};
