#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/music/Music_Service.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// ミュージックの曲を集める(画面にも MusicService の「続きから」にも依存しない)。docs/design/sound.md「曲のタグと索引」。
//
// - フォルダと曲の一覧: /music/ の直下のフォルダ(名前順、16個まで)と、その中の *.mml / *.wav / *.mp3 / *.flac(名前順、64曲まで)。
//   以前は MusicScene の中にあったもの(「続きから」が画面無しで同じ並びを作れるように、ここへ移した)
// - **索引** /sys/music_index.bin: 曲1つにつき決まった大きさ(Record、260バイト)の記録を並べたもの。
//   フォルダ名・ファイル名・大きさ・更新日時・タグ(曲名・アーティスト・アルバム・曲番号)を持つ。
//   アーティスト別・アルバム別の一覧と、一覧の曲名(タグがあればタグの曲名)はここから引く。RAMへは丸ごと載せず、毎回頭から読む
// - **索引を作り直す**(IndexUpdater): 少しずつ(Step() を毎フレーム、1回 budget_ms まで)。前の索引に同じフォルダ・名前・大きさ・
//   更新日時の記録があればタグを読み直さずに写す(前の索引の場所はハッシュの表で引く。表は作り直す間だけ malloc、約7KB)。
//   新しい索引は .tmp へ書き、できあがったら差し替える(途中で止めても前の索引は残る)
namespace MusicLibrary {
    using Track = MusicService::Track;
    static constexpr int kMaxFolders = MusicService::kMaxFolders;
    static constexpr int kMaxTracks = MusicService::kMaxTracks;
    // 索引に入れる曲の数の上限(これより多ければ、後から見つけた曲は入れない)
    static constexpr int kMaxIndexed = 512;

    // ---- フォルダと曲 ----
    // /music/ の直下のフォルダを名前順に out へ。数を返す
    int ScanFolders(FixedString<PICO_STR_M>* out, int max);
    // playlist(-1 = すべての曲、0以上 = folders の番号)の曲を名前順に out へ。数を返す
    int ScanTracks(const FixedString<PICO_STR_M>* folders, int folder_count, int playlist, Track* out, int max);

    // ---- 索引 ----
    struct Record {
        char folder[48];    // フォルダ名(/music/ の直下なら空)
        char file[48];      // ファイル名
        char title[56];     // タグ(無ければ空。UTF-8を途中で切らない)
        char artist[48];
        char album[48];
        uint32_t size;      // ファイルの大きさと更新日時(変わっていなければタグを読み直さない)
        uint16_t date;
        uint16_t time;
        uint16_t track;     // 曲番号(無ければ0)
        uint16_t reserved;
    };
    static_assert(sizeof(Record) == 260, "索引の記録の大きさを変えたら kIndexVersion を上げる");

    enum class Group : uint8_t { Artist, Album };

    // 索引の曲の数(無い・壊れていれば0)
    int IndexCount();
    // i 番目の記録
    bool ReadRecord(int i, Record& out);
    // アーティスト/アルバムの名前を重ならないように名前順で out へ(タグが無い曲は空の名前=「不明」に入る)。数を返す
    int ListGroups(Group g, FixedString<PICO_STR_M>* out, int max);
    // そのアーティスト/アルバムの曲を out へ。folders に無いフォルダの曲は入れない。
    // 並びは、アルバムなら曲番号→ファイル名、アーティストならアルバム名→曲番号→ファイル名。数を返す
    int ListGroupTracks(Group g, const char* name, const FixedString<PICO_STR_M>* folders, int folder_count, Track* out, int max);
    // tracks の曲名(Track::title)を索引のタグの曲名で埋める(索引に無い曲は空のまま)
    void FillTitles(Track* tracks, int count, const FixedString<PICO_STR_M>* folders, int folder_count);

    // ---- 索引を作り直す ----
    // 始める(もう動いていれば何もしない)。作り直す間の表を確保できなければ false
    bool StartIndexUpdate();
    // 少し進める。終わったら(または動いていなければ)true
    bool StepIndexUpdate(uint32_t budget_ms);
    // やめる(作りかけの .tmp は消す。前の索引はそのまま)
    void CancelIndexUpdate();
    bool IndexUpdating();
    // 作り直している間に調べた曲の数
    int IndexUpdateDone();
    // 索引が差し替わるたびに増える(画面が一覧を作り直す合図)
    uint32_t IndexRevision();

    // UTF-8を途中で切らずに、cap-1 バイトまでを dst へ(終端を付ける)
    void CopyUtf8(char* dst, size_t cap, const char* src);
}
