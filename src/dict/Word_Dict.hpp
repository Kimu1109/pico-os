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
//   - 検索は2段階。まず1列目がクエリで「始まる」エントリ(前方一致)を
//     ブロックインデックス+二分探索で即座に返す(IME_Dict.cppと同じ発想)。
//     ソート済みなので前方一致は必ず連続した1ブロックに収まる。
//   - 前方一致だけではkMaxHitsを満たさない場合に限り、ファイル全体を
//     1フレームあたり一定行数ずつ読み進めて「語の途中に含む」一致を拾う
//     (Task同様、update()を毎フレーム呼ぶ想定。呼び出し側をブロックしない)。
//     26MB級のファイルを毎回全部読むと実機のSD(SPI 10MHz)では数十秒〜
//     数分かかり得るため、前方一致だけで足りる大半のケースでは全体走査
//     そのものを行わずに済ませるのが狙い。
//   - インデックスが無くても(あるいは壊れていても)全体走査だけで正しい
//     結果は得られる。インデックスは「速く返せる分だけ速く返す」ための
//     最適化で、正しさの根拠ではない(Doc_Fetchのマニフェストと同じ立て付け)。
//   - ヒープ確保を避けるため固定長バッファのみを使用する(String不使用)。
//
#include <SdFat.h>
#include <stdint.h>
#include <stddef.h>
#include "util/FixedString.hpp"
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
        Idle,      // search()未呼び出し
        Scanning,  // 前方一致ぶんはhits_に入っている。残りをupdate()で走査中
        Done,      // 走査完了(全体走査まで終わった、件数上限に達した、キャンセルされた等)
    };

    // lookup()が返す件数の上限。1件は最大で term(96B)+desc(1KiB)+α ≒ 1.1KB
    static constexpr int kMaxHits = 20;

    // ブロックインデックスの静的配列サイズ。実データ(28万行強、
    // block-size=800なら約360ブロック)に余裕を持たせてある。
    // script/build_dict_index.py 側もこの値を超えたら警告を出す。
    static constexpr int kMaxIndexEntries = 400;

    // インデックスへ保持する検索用語句の切り詰め長。
    // 長い検索用語句(まれ。実データで48Bを超えるのは0.6%程度)は
    // ここで切り詰まるが、二分探索が多少不正確なブロックへ飛ぶだけで
    // 最終的な正しさは全体走査側が担保するため実害は無い。
    static constexpr size_t kIndexKeyBytes = 48;

    // update()を1回呼ぶごとに読み進める行数。SD側の帯域(SPI 10MHz)が
    // 律速なので大きくしても全体の走査時間は変わらないが、1フレーム
    // あたりの停止時間を抑えるためにここで刻む。
    static constexpr int kLinesPerUpdate = 200;

    // dictPath: 辞書tsvファイルパス(検索用語句でソート済み)
    // indexPath: build_dict_index.py が生成したブロックインデックス
    // 戻り値: 辞書ファイルが開けたらtrue。インデックスが無くても
    //   (0件でも)全体走査だけで動くようbeginは失敗にしない。
    bool begin(const char* dictPath, const char* indexPath);

    // 新しい検索を始める。前方一致ぶんは同期的にhits_へ積む
    // (ブロック内の線形走査だけなので実機でも一瞬で終わる)。
    // 前方一致だけでkMaxHitsに達した場合はそのままDoneになり、
    // 全体走査は行わない。
    void search(const char* query);

    // 全体走査を1歩だけ進める。stateがScanning以外なら何もしない。
    void update();

    // 全体走査を打ち切る(以降update()を呼んでも進まない)。
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

    // keyより小さいか等しい、最後のインデックスエントリのインデックスを返す
    int findBlockStart(const char* key) const;

    // 1行読み、column1/2/3の開始位置を返す(改行は取り除きnul終端する)。
    // 戻り値: 読めたバイト数(0以下ならEOF)。バッファに収まらないほど
    // 長い行(実データで3KB程度のものが極めて稀に存在する)は、col1/col2
    // (行の先頭にあるので大抵はバッファ内に収まる)はそのまま使い、
    // col3(説明)がバッファをはみ出した分だけ切り詰まる。はみ出した残りは
    // 改行に達するまで読み捨てて、次の行の開始位置とずれないようにする。
    int readLine(char* buf, int bufSize, char** col1, char** col2, char** col3);

    void addHit(const char* col2, const char* col3);

    SdFat*  sd_ = nullptr;
    FsFile  dictFile_;
    char    dictPath_[64] = {0};
    uint32_t fileSize_ = 0;

    IndexEntry index_[kMaxIndexEntries];
    int      indexCount_ = 0;

    WordDictHit hits_[kMaxHits];
    int         hitCount_ = 0;

    State state_ = State::Idle;

    // 検索語(英字はここで小文字化して保持する。検索用語句側も
    // 英単語は小文字化されて格納されている前提のため)
    char   query_[PICO_STR_M];
    size_t queryLen_ = 0;

    // 全体走査の現在位置と、前方一致パスが既に確認し終えた範囲
    // ([prefixRangeStart_, prefixRangeEnd_))。全体走査がこの範囲に
    // 入ったら、二重にヒットを数えないよう一気に読み飛ばす。
    uint32_t scanOffset_ = 0;
    uint32_t prefixRangeStart_ = 0;
    uint32_t prefixRangeEnd_ = 0;
    bool     skippedPrefixRange_ = false;
};
