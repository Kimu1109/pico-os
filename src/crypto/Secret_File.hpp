#pragma once

#include <cstddef>

// PICO_Secret の平文の上限(255B)より長い秘密(OAuthの更新用トークン・OpenAIのAPIキー等)を、
// SDの1ファイルへ「暗号化した行」に分けて置く。
//
//   # 見出し(コメント)
//   enc3:…   ← 1行目(用途の文字列は "<purpose>-0")
//   enc3:…   ← 2行目("<purpose>-1")
//
// - 行ごとに用途の文字列を変える(同じ鍵ストリームを使い回すと、2行をXORして平文同士のXORが漏れる)
// - **母艦で平文のまま1行で書いて置いてもよい**(接頭辞の無い行はそのまま読む)。読んだときに
//   needs_rewrite が立つので、呼ぶ側が Save() で暗号化して書き直す(Wi-Fi・Todoistのトークンと同じ流儀)
// - 書くときは .tmp へ書いてから入れ替える(途中で電源が切れても前のものが残る)
namespace PICO_SecretFile {
    // 読む。空行と # の行は飛ばす。読めたら(1バイト以上)true。壊れていたら out を空にして false。
    // needs_rewrite: 平文の行や古い鍵の行があった(今の鍵で書き直すべき)
    bool Load(const char* path, const char* purpose, char* out, size_t cap, bool* needs_rewrite = nullptr);
    // 書く。header は先頭のコメント1行("# " は付けて渡す。nullptrなら無し)
    bool Save(const char* path, const char* purpose, const char* header, const char* plain);
    // 消す(無ければ何もしない)
    void Remove(const char* path);
}
