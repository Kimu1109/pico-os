#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

// サーバのマニフェスト(`PROTOCOL.md`「4. マニフェスト」)を引くための層。
//
// 中身は1行1文書の `path<TAB>version` で、**pathの昇順**に並んでいる。
// 取得と保存は discovery と同じく Doc_Fetch / Doc_Cache をそのまま通すので、
// ここが相手にするのは「SDへ落ちた後のファイル」だけになる。
//
// **狙いは「開くたびの条件付きGETを省く」こと。** 手元のキャッシュの検証子と
// マニフェストのversionが一致していれば、その文書はサーバへ何も聞かずに
// そのまま開ける(304の往復すら要らない)。一致しない/載っていない場合は
// 今までどおり条件付きGETへ落ちるので、**マニフェストが無くても壊れない**。
namespace Manifest {

    // マニフェストから path の検証子を引く。
    //
    // pathの昇順という取り決めを使って**追い越した時点で打ち切る**ので、
    // 全体をRAMへ載せずに済む(`PROTOCOL.md`が昇順を必須にしている理由)。
    // 見つからない場合と、検証子が out に収まらない場合は false
    // (切り詰めて比べると別物を同じと見なしてしまうため)。
    bool VersionOf(const char* manifest_file, const char* path,
                   FixedString<PICO_STR_M>& out);
}
