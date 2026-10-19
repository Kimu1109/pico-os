#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

// サーバ情報(`GET /.well-known/pico-os` の中身)。PROTOCOL.md「サーバ情報」参照。
//
// **discoveryは特別扱いせず、ただの文書として取る。** 置き場所が
// /.well-known/pico-os という決め打ちなだけで、取得も保存も Doc_Fetch /
// Doc_Cache をそのまま通す。おかげで条件付きGET(304)も、圏外のときに
// 前回の内容を使うことも、何も書かずに手に入る。
struct ServerInfo {
    // このinfoがどのサーバのものか("host" または "host:port")
    FixedString<PICO_STR_M> host;

    int version = 0;                  // サーバが実装するプロトコルの版
    FixedString<PICO_STR_M> name;     // 表示名
    FixedString<PICO_STR_L> home;     // 既定の文書のパス
    FixedString<PICO_STR_L> search;   // 検索エンドポイント。**空なら検索非対応**
    FixedString<PICO_STR_L> manifest; // マニフェストエンドポイント

    // 1度問い合わせて結果が確定したか。**404でもtrue**
    // (「素の静的ファイルサーバ」と分かった、という確定した結果なので、
    //  ページを開くたびに問い合わせ直さない)
    bool checked = false;

    bool hasSearch() const { return !search.empty(); }

    void clear(){
        host.clear();
        version = 0;
        name.clear();
        home.clear();
        search.clear();
        manifest.clear();
        checked = false;
    }
};

namespace Discovery {

    // discoveryの置き場所。PROTOCOL.mdで決め打ち
    constexpr const char* kPath = "/.well-known/pico-os";

    // 目録1行の上限
    constexpr int kMaxLineLen = 256;

    // "キー<TAB>値" の1行を解釈してinfoへ反映する。
    // **知らないキーは無視する**(サーバが将来キーを足しても古いクライアントが
    //  壊れないための、PROTOCOL.mdの前方互換ルール)。
    // lineは書き換えられる(タブをNULへ置き換えるため)。
    void ParseLine(char* line, ServerInfo& out);

    // SD上のTSVを読んでinfoへ詰める。開けなければfalse
    bool ParseFile(const char* path, ServerInfo& out);
}
