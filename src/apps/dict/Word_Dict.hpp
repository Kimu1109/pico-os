#pragma once
//
// Word_Dict.hpp
// script/en-ja-and-ja-en.tsv 形式(英和/和英統合の単語辞書)向けの
// 部分一致検索モジュール。
//
// ファイル形式:
//   1行 = 「検索用語句 \t 表示用語句 \t 説明or訳」。検索用語句のバイト順で
//   ソート済み(script/build_dict_index.py が前提にする)。英単語は
//   「小文字化した綴り \t 元の綴り \t 日本語の説明」、日本語単語は
//   「ひらがな読み \t 元の表記 \t 対応する英単語」という中身になる
//   (このクラスは中身の意味までは見ず、3列目までタブで割るだけ)。
//   部分一致の対象は常に1列目(検索用語句)のみ。
//
// 設計方針:
//   - 検索は3段階。まず1列目がクエリで「始まる」エントリ(前方一致)を
//     ブロックインデックス+二分探索で即座に返す(IME_Dict.cppと同じ発想)。
//     ソート済みなので前方一致は必ず連続した1ブロックに収まる。
//   - 前方一致だけではkMaxHitsを満たさない場合、次に「語の途中に含む」
//     一致(部分一致)を試す。サフィックスインデックス(dict_suffixes.tsv +
//     dict_suffix_index.tsv、script/build_dict_suffix_index.pyが生成)が
//     あれば、これも前方一致と同じブロックインデックス+二分探索の技法で
//     引ける — 「各検索用語句の先頭以外から始まる全接尾辞」をバイト順に
//     ソートしたコーパスなので、「語の途中に含む」検索は「あるエントリの
//     接尾辞に対する前方一致」に帰着できるため。ただし本体の索引より
//     ブロックが粗い(1ブロックの行数が多い)ぶん1ブロックの確認に
//     時間がかかりうるので、前方一致と違い同期では終わらせず
//     State::ScanningSuffixとしてupdate()でフレーム分割する。
//   - サフィックスインデックスが無い(あるいは壊れている)場合のみ、
//     ファイル全体を1フレームあたり一定行数ずつ読み進めて拾う
//     (State::Scanning。Task同様、update()を毎フレーム呼ぶ想定)。
//     26MB級のファイルを毎回全部読むと実機のSD(SPI 10MHz)では数十秒〜
//     数分かかり得るため、この経路は最後の保険として位置づける。
//   - どちらのインデックスも「速く返せる分だけ速く返す」ための最適化で、
//     正しさの根拠ではない(Doc_Fetchのマニフェストと同じ立て付け)。
//     無くても(壊れていても)全体走査だけで正しい結果は得られる。
//   - クエリが1文字だけの部分一致は一致数が多すぎて実用的な絞り込みに
//     ならないため、kMinSubstringQueryChars未満なら部分一致自体を
//     試みず前方一致の結果のみ返す。
//   - ヒープ確保を避けるため固定長バッファのみを使用する(String不使用)。
//
#include <SdFat.h>
#include <stdint.h>
#include <stddef.h>
#include "util/FixedString.hpp"
#include "util/Utf8Byte.hpp"
#include "consts.hpp"

// 検索結果1件。表示用語句と説明/訳のみを持つ(検索用語句そのものは
// 表示に使わないので保持しない)。
struct WordDictHit {
    FixedString<PICO_STR_L>   term; // 表示用語句(2列目。元の表記)
    FixedString<PICO_STR_1KiB> desc; // 説明or訳(3列目)。長文はここで切り詰まる
};

class WordDictionary {
public:
    enum class State : uint8_t {
        Idle,           // search()未呼び出し
        Scanning,       // 前方一致ぶんはhits_に入っている。残りをファイル全体走査で拾い中
                        // (サフィックスインデックスが無い/壊れている場合のみ通る経路)
        ScanningSuffix, // サフィックスインデックスのブロックを走査して部分一致を拾い中
        Done,           // 走査完了(件数上限に達した、キャンセルされた等を含む)
    };

    // lookup()が返す件数の上限。1件は最大で term(96B)+desc(1KiB)+α ≒ 1.1KB
    static constexpr int kMaxHits = 20;

    // ブロックインデックスの静的配列サイズ。実データ(28万行強、
    // block-size=800なら約360ブロック)に余裕を持たせてある。
    // script/build_dict_index.py 側もこの値を超えたら警告を出す。
    static constexpr int kMaxIndexEntries = 400;

    // サフィックスインデックス(部分一致用)のブロック配列サイズ。
    // サフィックスコーパスは本体よりずっと行数が多い(実データで本体27万行
    // に対し接尾辞144万行)分をscript/build_dict_suffix_index.py側の
    // block-sizeで吸収する設計のため、本体索引(400)より少なく抑えてRAMを
    // 余分に食わせないようにしてある(実データでblock-size自動算出=8492、
    // 170ブロック)。
    static constexpr int kMaxSuffixIndexEntries = 200;

    // インデックスへ保持する検索用語句/接尾辞の切り詰め長。
    // 長い検索用語句(まれ。実データで48Bを超えるのは0.6%程度)は
    // ここで切り詰まるが、二分探索が多少不正確なブロックへ飛ぶだけで
    // 最終的な正しさは全体走査側(本体索引)/ブロック内の全件確認
    // (サフィックス索引)が担保するため実害は無い。
    static constexpr size_t kIndexKeyBytes = 48;

    // 部分一致(サフィックス)検索が有効になる最小文字数(UTF-8文字数)。
    // 1文字だと一致数が多すぎて絞り込みとして機能しないため、これ未満は
    // 部分一致を試みず前方一致の結果のみ返す。
    static constexpr int kMinSubstringQueryChars = 2;

    // update()を1回呼ぶごとに読み進める行数。SD側の帯域(SPI 10MHz)が
    // 律速なので大きくしても全体の走査時間は変わらないが、1フレーム
    // あたりの停止時間を抑えるためにここで刻む。Scanning/ScanningSuffix
    // 共通(サフィックス側は1ブロックの行数が多いぶん複数フレームに
    // またがることがある)。
    static constexpr int kLinesPerUpdate = 200;

    // dictPath: 辞書tsvファイルパス(検索用語句でソート済み)
    // indexPath: build_dict_index.py が生成したブロックインデックス
    // suffixDictPath/suffixIndexPath: build_dict_suffix_index.py が生成した
    //   部分一致用のサフィックスコーパス/そのブロックインデックス。省略
    //   (nullptr)や読み込み失敗時は部分一致がファイル全体走査
    //   (State::Scanning)へ自動フォールバックするだけなので、begin()を
    //   失敗にはしない。
    // 戻り値: 辞書ファイルが開けたらtrue。インデックスが無くても
    //   (0件でも)全体走査だけで動くようbeginは失敗にしない。
    bool begin(const char* dictPath, const char* indexPath,
               const char* suffixDictPath = nullptr, const char* suffixIndexPath = nullptr);

    // 新しい検索を始める。前方一致ぶんは同期的にhits_へ積む
    // (ブロック内の線形走査だけなので実機でも一瞬で終わる)。
    // 前方一致だけでkMaxHitsに達した場合はそのままDoneになり、
    // 全体走査は行わない。
    void search(const char* query);

    // 走査(全体走査またはサフィックスブロック走査)を1歩だけ進める。
    // stateがScanning/ScanningSuffix以外なら何もしない。
    void update();

    // 走査を打ち切る(以降update()を呼んでも進まない)。
    // 既に見つかっている hits_ はそのまま残す。
    void cancel();

    State state() const { return state_; }
    int count() const { return hitCount_; }
    const WordDictHit& hit(int index) const { return hits_[index]; }

    // 件数上限で打ち切った(=まだ他にも一致がある可能性がある)
    bool mayHaveMore() const { return hitCount_ >= kMaxHits; }

    // 全体走査の進み具合(0.0〜1.0)。前方一致だけで完了した場合は1.0。
    // 「検索中 43%」のような進捗表示に使う想定。
    float progress() const;

private:
    struct IndexEntry {
        char     key[kIndexKeyBytes];
        uint32_t offset;
    };

    bool loadIndex(const char* indexPath);
    // suffixIndex_の読み込み版。失敗してもsuffixAvailable_をfalseのまま
    // 残すだけ(全体走査へフォールバックするため)。
    bool loadSuffixIndex(const char* indexPath);
    // loadIndex()/loadSuffixIndex()共通の本体。「検索用語句(または接尾辞)
    // <TAB>バイトオフセット」形式の行をarrへ最大maxCount件読み込み、
    // 読めた件数を返す。
    static int LoadIndexEntries(SdFat* sd, const char* indexPath,
                                 IndexEntry* arr, int maxCount);

    // keyより小さいか等しい、最後のインデックスエントリのインデックスを返す。
    // 本体索引(index_)とサフィックス索引(suffixIndex_)の両方から呼ぶため
    // 配列とその件数を引数で受け取る(countが0なら呼び出し側で分岐すること)。
    static int findBlockStart(const IndexEntry* arr, int count, const char* key);

    // 1行読み、column1/2/3の開始位置を返す(改行は取り除きnul終端する)。
    // 戻り値: 読めたバイト数(0以下ならEOF)。バッファに収まらないほど
    // 長い行(実データで3KB程度のものが極めて稀に存在する)は、col1/col2
    // (行の先頭にあるので大抵はバッファ内に収まる)はそのまま使い、
    // col3(説明)がバッファをはみ出した分だけ切り詰まる。はみ出した残りは
    // 改行に達するまで読み捨てて、次の行の開始位置とずれないようにする。
    int readLine(char* buf, int bufSize, char** col1, char** col2, char** col3);

    // サフィックスコーパス(接尾辞<TAB>元エントリの行頭オフセット)を1行読む。
    // readLine()と同じ「収まらない行は読み捨てて次行とずれないようにする」
    // ガードを踏襲する。
    int readSuffixLine(char* buf, int bufSize, char** suffix, uint32_t* origOffset);

    void addHit(const char* col2, const char* col3);

    // 全体走査(State::Scanning)を1歩進める。旧来のフォールバック経路。
    void updateFullScan();
    // サフィックスブロック走査(State::ScanningSuffix)を1歩進める。
    void updateSuffixScan();

    // originalOffset(dictFile_内の行頭オフセット)が既にhits_へ積み済みかを返す。
    // 同じエントリが複数の接尾辞位置で一致しても2重にヒットさせないための照合。
    bool isOffsetAlreadyAdded(uint32_t originalOffset) const;
    void rememberAddedOffset(uint32_t originalOffset);

    SdFat*  sd_ = nullptr;
    FsFile  dictFile_;
    char    dictPath_[64] = {0};
    uint32_t fileSize_ = 0;

    IndexEntry index_[kMaxIndexEntries];
    int      indexCount_ = 0;

    // ---- 部分一致(サフィックス)検索 ----
    FsFile     suffixFile_;
    uint32_t   suffixFileSize_ = 0;
    IndexEntry suffixIndex_[kMaxSuffixIndexEntries];
    int        suffixIndexCount_ = 0;
    bool       suffixAvailable_ = false; // ファイル+索引の両方が開けたか

    // 走査中のブロックの範囲([suffixBlockStart_, suffixBlockEnd_))。
    // progress()の分母に使う(ファイル全体ではなく「このブロックの中で
    // どこまで進んだか」を示す — ブロックはファイル全体のごく一部なので、
    // 全体基準だと割合がほぼ動かずUIとして意味を成さないため)。
    uint32_t suffixBlockStart_ = 0;
    uint32_t suffixBlockEnd_ = 0;
    uint32_t suffixScanOffset_ = 0;
    // ソート済みコーパスで前方一致グループへ既に入ったか。抜けたら
    // (=クエリで始まらない行が来たら)それ以上探す必要はない。
    bool     suffixInGroup_ = false;

    // hits_と対になる「どのdictFile_オフセットから作ったか」の記録。
    // サフィックス経由の重複排除にのみ使う(前方一致/全体走査は行を
    // 1回ずつしか読まないため元々重複しない)。
    uint32_t addedOffsets_[kMaxHits];
    int      addedOffsetsCount_ = 0;

    WordDictHit hits_[kMaxHits];
    int         hitCount_ = 0;

    State state_ = State::Idle;

    // 検索語(英字はここで小文字化して保持する。検索用語句側も
    // 英単語は小文字化されて格納されている前提のため)
    char   query_[PICO_STR_M];
    size_t queryLen_ = 0;

    // 全体走査の現在位置と、前方一致パスが既に確認し終えた範囲
    // ([prefixRangeStart_, prefixRangeEnd_))。全体走査/サフィックス走査が
    // この範囲に入ったら、二重にヒットを数えないよう読み飛ばす(全体走査は
    // 連続領域なので一気にseek、サフィックス走査は行ごとにオフセット
    // 照合。詳細はWord_Dict.cpp参照)。
    uint32_t scanOffset_ = 0;
    uint32_t prefixRangeStart_ = 0;
    uint32_t prefixRangeEnd_ = 0;
    bool     skippedPrefixRange_ = false;
};
