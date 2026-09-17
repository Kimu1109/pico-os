// 単語辞書(script/en-ja-and-ja-en.tsv形式)の部分一致検索(WordDictionary)のテスト。
//
// 確かめたいのは:
//   - 前方一致は二分探索+ブロック内走査で即座に(update()を呼ばずに)拾えること
//   - 語の途中に含む一致(前方一致ではない)はupdate()を繰り返すことで拾えること
//   - 前方一致パスと全体走査パスの範囲がちょうど1回ずつしか数えられない(重複しない)こと
//   - 件数上限(kMaxHits)で正しく打ち切られること(mayHaveMore())
//   - 英字クエリの大文字/小文字を吸収すること、日本語(ひらがな)もそのまま検索できること
//   - 読み込みバッファに収まらない極端に長い行があっても、後続行がずれずに読めること
//   - インデックスが無くても(全体走査だけで)正しい結果が得られること
//
// SDはstubs/SdFat.hのパス->内容のmapなので、HostSd::filesへ直接テスト用データを積む。
#include "dict/Word_Dict.hpp"
#include "OS_Data.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static int failures = 0;

static void check(bool cond, const char* label) {
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if (!cond) failures++;
}

static void eq_int(int actual, int expected, const char* label) {
    const bool ok = (actual == expected);
    printf("%s %-56s 実測=%d 期待=%d\n", ok ? "[ OK ]" : "[FAIL]", label, actual, expected);
    if (!ok) failures++;
}

static void reset() { HostSd::files.clear(); }

// ---- テスト用辞書データの組み立て ----

struct Entry {
    std::string term;  // 検索用語句(col1)
    std::string disp;  // 表示用語句(col2)
    std::string desc;  // 説明or訳(col3)
};

static const char* kDictPath = "/sys/dict/test_dict.tsv";
static const char* kIndexPath = "/sys/dict/test_dict_index.tsv";

// entries(呼び出し前にterm順へソート済みであること)からTSV本体を組み立て、
// SDへ登録する。block_size行ごとにインデックスも作る(0なら作らない)。
static void writeDict(std::vector<Entry> entries, int block_size) {
    std::string body;
    std::string index;
    uint32_t offset = 0;

    for (size_t i = 0; i < entries.size(); i++) {
        if (block_size > 0 && (i % (size_t)block_size) == 0) {
            index += entries[i].term + "\t" + std::to_string(offset) + "\n";
        }
        std::string line = entries[i].term + "\t" + entries[i].disp + "\t" + entries[i].desc + "\n";
        body += line;
        offset += (uint32_t)line.size();
    }

    HostSd::files[kDictPath] = body;
    if (block_size > 0) {
        HostSd::files[kIndexPath] = index;
    }
}

static std::vector<Entry> baseEntries() {
    // term順(バイト順)でソートしておく必要がある。書き出し前にsortもするので
    // ここでの並びは気にしなくてよい。
    std::vector<Entry> v = {
        {"book",        "book",        "a written or printed work / 本"},
        {"bookcase",    "bookcase",    "a piece of furniture / 本棚"},
        {"cat",         "cat",         "a small domesticated animal / 猫"},
        {"category",    "category",    "a class or division / 部門"},
        {"concatenate", "concatenate", "to link together / 連結する"},
        {"place",       "place",       "a particular location / 場所"},
        {"copy",        "copy",        "to duplicate / 複製"},
        {"ねこ",         "猫",          "cat"},
        {"こねこ",       "子猫",         "kitten"},
        {"あい",         "愛",          "love"},
    };
    std::sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) { return a.term < b.term; });
    return v;
}

int main() {
    // ---- 前方一致(英語): 二分探索+ブロック内走査で即座に拾える ----
    {
        reset();
        writeDict(baseEntries(), /*block_size=*/2);

        WordDictionary dict;
        check(dict.begin(kDictPath, kIndexPath), "begin: 辞書とインデックスを開ける");

        dict.search("cat");
        // "cat" と "category" が前方一致。"concatenate" は前方一致ではない(語の途中)。
        eq_int(dict.count(), 2, "前方一致'cat': 件数は2件(cat, category)");
        check(dict.state() != WordDictionary::State::Idle, "前方一致'cat': search()後はIdleでない");

        bool foundCat = false, foundCategory = false, foundConcat = false;
        for (int i = 0; i < dict.count(); i++) {
            const char* t = dict.hit(i).term.c_str();
            if (strcmp(t, "cat") == 0) foundCat = true;
            if (strcmp(t, "category") == 0) foundCategory = true;
            if (strcmp(t, "concatenate") == 0) foundConcat = true;
        }
        check(foundCat && foundCategory, "前方一致'cat': 中身がcat/categoryそのもの");
        check(!foundConcat, "前方一致'cat': 語の途中一致(concatenate)はこの時点では混ざらない");
    }

    // ---- 部分一致(英語): 語の途中に含む一致はupdate()で拾う ----
    {
        reset();
        writeDict(baseEntries(), /*block_size=*/2);

        WordDictionary dict;
        check(dict.begin(kDictPath, kIndexPath), "begin (部分一致テスト)");

        dict.search("cat");
        eq_int(dict.count(), 2, "'cat'検索直後: 前方一致2件のまま");
        check(dict.state() == WordDictionary::State::Scanning,
              "'cat'検索直後: 全体走査待ち(Scanning)になる");

        int guard = 0;
        while (dict.state() == WordDictionary::State::Scanning && guard++ < 10000) {
            dict.update();
        }
        check(dict.state() == WordDictionary::State::Done, "全体走査: 最終的にDoneになる");
        eq_int(dict.count(), 3, "'cat'検索完了後: concatenateぶんが増えて3件");

        bool foundConcat = false;
        int concatCount = 0;
        for (int i = 0; i < dict.count(); i++) {
            const char* t = dict.hit(i).term.c_str();
            if (strcmp(t, "concatenate") == 0) { foundConcat = true; concatCount++; }
        }
        check(foundConcat, "全体走査: concatenate(語の途中の一致)が見つかる");
        eq_int(concatCount, 1, "全体走査: concatenateは1回しか数えられない(前方一致範囲との重複無し)");
    }

    // ---- 大文字/小文字を吸収する ----
    {
        reset();
        writeDict(baseEntries(), /*block_size=*/2);

        WordDictionary dict;
        dict.begin(kDictPath, kIndexPath);
        dict.search("CAT");
        eq_int(dict.count(), 2, "大文字クエリ'CAT': 小文字化されてcat/categoryにヒットする");
    }

    // ---- 日本語(ひらがな)の部分一致 ----
    {
        reset();
        writeDict(baseEntries(), /*block_size=*/2);

        WordDictionary dict;
        dict.begin(kDictPath, kIndexPath);
        dict.search("ねこ");
        eq_int(dict.count(), 1, "'ねこ'検索直後: 前方一致は自分自身の1件のみ");

        int guard = 0;
        while (dict.state() == WordDictionary::State::Scanning && guard++ < 10000) {
            dict.update();
        }
        eq_int(dict.count(), 2, "'ねこ'検索完了後: こねこ(語の途中の一致)ぶんが増えて2件");

        bool foundKoneko = false;
        for (int i = 0; i < dict.count(); i++) {
            if (strcmp(dict.hit(i).term.c_str(), "子猫") == 0) foundKoneko = true;
        }
        check(foundKoneko, "'ねこ'検索完了後: こねこ(子猫)が語の途中一致として見つかる");
    }

    // ---- 一致無し ----
    {
        reset();
        writeDict(baseEntries(), /*block_size=*/2);

        WordDictionary dict;
        dict.begin(kDictPath, kIndexPath);
        dict.search("xyzzy_no_such_word");

        int guard = 0;
        while (dict.state() == WordDictionary::State::Scanning && guard++ < 10000) {
            dict.update();
        }
        check(dict.state() == WordDictionary::State::Done, "一致無し: 最終的にDoneになる");
        eq_int(dict.count(), 0, "一致無し: 0件");
    }

    // ---- 件数上限(kMaxHits)で正しく打ち切られる ----
    {
        reset();
        std::vector<Entry> entries;
        // "zzzmatch"を語の途中に含むエントリを25件(kMaxHits=20より多く)作る。
        // 前方一致ではなく、必ず全体走査側で拾われるようにする(先頭に別文字を付ける)。
        for (int i = 0; i < 25; i++) {
            char term[32], disp[32];
            snprintf(term, sizeof(term), "x%02dzzzmatch", i);
            snprintf(disp, sizeof(disp), "X%02dZzzMatch", i);
            entries.push_back({term, disp, "dummy"});
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.term < b.term; });
        writeDict(entries, /*block_size=*/4);

        WordDictionary dict;
        dict.begin(kDictPath, kIndexPath);
        dict.search("zzzmatch");
        eq_int(dict.count(), 0, "件数上限テスト: 前方一致は無い(全て語の途中一致)");
        check(dict.state() == WordDictionary::State::Scanning, "件数上限テスト: 全体走査待ちになる");

        int guard = 0;
        while (dict.state() == WordDictionary::State::Scanning && guard++ < 10000) {
            dict.update();
        }
        check(dict.state() == WordDictionary::State::Done, "件数上限テスト: Doneになる");
        eq_int(dict.count(), WordDictionary::kMaxHits, "件数上限テスト: kMaxHitsぴったりで打ち切られる");
        check(dict.mayHaveMore(), "件数上限テスト: mayHaveMore()がtrueになる(25件中20件しか返していない)");
    }

    // ---- 読み込みバッファに収まらない極端に長い行があっても後続行がずれない ----
    {
        reset();
        std::vector<Entry> entries = baseEntries();
        // PICO_STR_2KiB(読み込みバッファ)を超える説明文を持つダミーエントリを挟む。
        // col1/col2(検索・表示に使う列)は行の先頭にあるため通常は保持されるが、
        // col3(説明)はバッファをはみ出した分が切り詰まる。改行に達するまで
        // 読み捨てて同期を取り直すため、直後の行が巻き込まれてずれないことを確かめる
        // (このエントリ自身のtermには"cat"を含めない。直後の行にだけ含めて、
        //  同期がずれていればその行が正しく拾えなくなることで検知する)。
        std::string hugeDesc(4000, 'x');
        entries.push_back({"zz1_overlong", "overlong", hugeDesc});
        entries.push_back({"zz2_catnip", "catnip", "a plant cats love / またたび"});
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.term < b.term; });
        writeDict(entries, /*block_size=*/2);

        WordDictionary dict;
        dict.begin(kDictPath, kIndexPath);
        dict.search("cat");
        int guard = 0;
        while (dict.state() == WordDictionary::State::Scanning && guard++ < 10000) {
            dict.update();
        }
        check(dict.state() == WordDictionary::State::Done, "極端に長い行: 全体走査は最後まで完了する(異常終了しない)");

        bool foundConcat = false, foundCategory = false, foundCatnip = false;
        for (int i = 0; i < dict.count(); i++) {
            const char* t = dict.hit(i).term.c_str();
            if (strcmp(t, "concatenate") == 0) foundConcat = true;
            if (strcmp(t, "category") == 0) foundCategory = true;
            if (strcmp(t, "catnip") == 0) foundCatnip = true;
        }
        check(foundConcat && foundCategory,
              "極端に長い行: 前方の通常の行は巻き込まれずに正しく読める");
        check(foundCatnip,
              "極端に長い行: 直後の行も同期がずれずに正しく読める(zz2_catnip)");
    }

    // ---- インデックスが無くても(全体走査だけで)正しい結果が得られる ----
    {
        reset();
        writeDict(baseEntries(), /*block_size=*/0); // インデックス無し

        WordDictionary dict;
        check(dict.begin(kDictPath, kIndexPath), "インデックス無し: begin()自体は失敗しない");

        dict.search("cat");
        int guard = 0;
        while (dict.state() == WordDictionary::State::Scanning && guard++ < 10000) {
            dict.update();
        }
        check(dict.state() == WordDictionary::State::Done, "インデックス無し: 最終的にDoneになる");
        eq_int(dict.count(), 3, "インデックス無し: それでもcat/category/concatenateの3件が見つかる");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
