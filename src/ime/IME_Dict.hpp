#pragma once
//
// IME_Dict.hpp
// SKK-JISYO.L(unannotated)ベースのIME辞書検索モジュール
// 「第3案：直接キー生成方式」に基づく実装
//
// 設計方針:
//   - 送りあり/なしを区別せず、単一の統合辞書ファイル・単一インデックスで検索する
//     (SKK-JISYO.L自体が「語幹+子音マーカー」形式のキーで配布されているため、
//      PC側での辞書分割・再加工は不要)
//   - 送りの処理は呼び出し側(基本的にLua層)で「語幹 + 子音マーカー」形式の
//     キーに変換済みであることを前提とする(例: "あゆ" + "む" → "あゆm")
//   - ヒープ確保を避けるため、固定長バッファのみを使用する(String不使用)
//   - SdFatライブラリ(FsFile)を前提とする
//

#include <SdFat.h>
#include <stdint.h>
#include <stddef.h>

// ---- 設定値 ----

// インデックスの1エントリが表す辞書側の行数
// (PC側の変換スクリプトでのブロック分割サイズと必ず一致させること)
#define IME_INDEX_BLOCK_LINES   320

// 読みがなキーの最大バイト数(ひらがな語幹 + 送りマーカー1文字 + null)
// ひらがなはUTF-8で1文字3バイト。長めの語幹でも十分な余裕を持たせる。
#define IME_MAX_KEY_BYTES       40

// 辞書1行の最大バイト数(読み + タブ + 候補複数 + 改行 + null)
// 候補数が多いエントリ(同音異義語が多い読み)もあるため余裕を持たせる
#define IME_MAX_LINE_BYTES      400

// 1候補(漢字表記)の最大バイト数
#define IME_MAX_CAND_BYTES      24

// lookup()が返す候補の最大数(候補バー: 4〜5件表示+overflow想定なので16あれば十分)
#define IME_MAX_CANDIDATES      16

// インデックスの最大エントリ数
// Mは64, MLは151が目安
#define IME_MAX_INDEX_ENTRIES   151

// 一致継続スキャンの安全上限(ブロック境界をまたぐケースを考慮した余裕値)
// これを超えて同一読みが続くことは通常想定しないための保険
#define IME_MAX_SCAN_LINES      2000


struct ImeIndexEntry {
    char     yomi[IME_MAX_KEY_BYTES]; // ブロック先頭行の読みがな(キー)
    uint32_t offset;                  // 辞書ファイル中のバイト位置
};

class ImeDictionary {
public:
    ImeDictionary();

    // dictPath: 辞書tsvファイルパス(読みがなでソート済み、送りあり/なし統合済み)
    // indexPath: インデックスtsvファイルパス(256行毎の先頭読みがな＋バイト位置)
    // 戻り値: 成功したらtrue
    bool begin(const char* dictPath, const char* indexPath);

    // key: 検索キー。
    //   送りありの場合は呼び出し側で「語幹＋子音マーカー」形式に変換済みで
    //   あること(例: "あゆm")。送りなしの場合はひらがな読みそのもの
    //   (例: "かんけつ")。
    // candidates: 出力先。各要素は IME_MAX_CAND_BYTES バイトの固定バッファ
    // maxCandidates: candidates配列の要素数上限(IME_MAX_CANDIDATES以下)
    // prefixMatch: trueなら「keyを前方一致で含む」候補も返す(デフォルト)。
    //   falseなら完全一致のみ。
    //   ファイルはよみ順にソート済みのため、完全一致エントリは必ず
    //   前方一致エントリより前(=辞書順で先)に来る。そのため
    //   prefixMatch=trueで返る候補は「完全一致優先、続けて前方一致」の
    //   順序が自動的に保たれる。候補バーの先頭数件を完全一致優先で
    //   表示したい場合もそのまま使える。
    // 戻り値: 見つかった候補数(0件ならヒットなし)
    int lookup(const char* key, char candidates[][IME_MAX_CAND_BYTES], int maxCandidates,
               bool prefixMatch = true);

private:
    SdFat*  _sd;
    FsFile  _dictFile;
    char    _dictPath[64];

    ImeIndexEntry _index[IME_MAX_INDEX_ENTRIES];
    int     _indexCount;

    bool loadIndex(const char* indexPath);

    // keyより小さいか等しい、最後のインデックスエントリのインデックスを返す
    // (見つからなければ0)
    int findBlockStart(const char* key);
};
