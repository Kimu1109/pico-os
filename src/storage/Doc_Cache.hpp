#pragma once

#include "SdFat.h"
#include "util/FixedString.hpp"
#include "storage/SD_Path.hpp"
#include "consts.hpp"

// Markdownブラウザのキャッシュ層。
//
// サーバから取った文書をSDへ書き、次回以降はSDから読む。MarkdownViewは
// 「SD上のファイルを開く」ことしか知らないままでよく、ネットワークとの境界が
// ファイルシステムで切れるのが狙い(片方が壊れてももう片方へ波及しない)。
//
// 配置は PROTOCOL.md のとおりサーバ上のパスをミラーする:
//
//     /cache/<正規化したホスト>/<サーバ上のパス>
//     例: /cache/192.168.1.10_8080/docs/pico/intro.md
//
// ハッシュ名にせずミラーするのは、FileExplorerでそのまま中身を覗けるようにするため。
// ただしFATでは ':' が使えず(ポート番号!)、大文字小文字も区別されないので、
// ホスト名はSanitizeHost()で正規化してから使う。
//
// 目録は /cache/index.tsv に置く。行指向TSVなのはSKK辞書や設定ファイルと同じ流儀で、
// 1行読むごとに1件処理でき、全体をRAMへ載せずに書き換えられるため。
namespace PICO_DocCache {

    // 目録1行のバイト数の上限。host+path+validator+数値2つが収まればよい
    constexpr int kMaxIndexLineLen = 512;

    // 1件あたりの最大バイト数。
    // PROTOCOL.mdの「巨大なレスポンス」対策で、サーバが何を返してきても
    // SDを埋め尽くさないようここで頭打ちにする(文書は8KiBだが画像は大きくなりうる)
    constexpr uint32_t kMaxEntryBytes = 64u * 1024u;

    // 目録1件。validatorはサーバのETag(無ければLast-Modified)をそのまま入れる
    struct Entry {
        FixedString<PICO_STR_M> host;      // 正規化済み(小文字、':'は'_')
        FixedString<PICO_STR_L> path;      // "/" 始まりのサーバ絶対パス
        FixedString<PICO_STR_M> validator; // 条件付きGETで送り返す値。空なら未取得扱い
        uint32_t fetched_epoch = 0;        // 取得時刻。NTP同期前は当てにならないので参考値
        uint32_t size = 0;                 // 本体のバイト数
    };

    // ホスト名をFATで使える形へ正規化する。
    // 小文字化し、FATで使えない文字(: * ? < > | " \ / と制御文字)を '_' へ置き換える。
    // 空文字や、PICO_STR_Mへ収まらない場合はfalse。
    bool SanitizeHost(FixedString<PICO_STR_M>& out, const char* host);

    // キャッシュ本体のSDパスを組み立てる
    bool PathFor(FixedString<PICO_PATH_LEN>& out, const char* host, const char* path);

    // 本体がSD上に存在するか(目録は見ない)
    bool Exists(const char* host, const char* path);

    // 目録から1件引く。見つからなければfalse
    bool Lookup(const char* host, const char* path, Entry& out);

    // 目録の1行を追加/差し替えする。
    // 元の目録を読みながら一時ファイルへ書き写し、最後に差し替える
    // (Config_Functions::SetValue()と同じ手順。全体をRAMへ載せないため)
    bool SetEntry(const Entry& entry);

    // 目録から1行消す(本体は消さない)
    bool RemoveEntry(const char* host, const char* path);

    // 本体と目録の両方を消す
    bool Remove(const char* host, const char* path);

    // キャッシュを全消去する(設定アプリの「キャッシュを削除」用)。
    // 追い出し(LRU等)は実装しない — 1文書8KiBに対しSDはGB単位あり、
    // 1000件貯めても8MB程度なので、枠を管理する対価に見合わない
    bool Clear();

    // 取得した本文をキャッシュへ書き込む。
    //
    // **通信が途中で切れた半端なファイルを「正常なキャッシュ」として残さない**のが
    // このクラスの存在理由。一時ファイル(.part)へ書き、commit()で初めて本来の名前へ
    // 差し替える(Config_Functions::SetValue()と同じ手順)。
    // commit()せずに破棄された場合はデストラクタが一時ファイルごと消す。
    //
    // 使い方:
    //     PICO_DocCache::Writer w;
    //     if(!w.begin(host, path)) ... ;
    //     while(受信中) if(!w.write(buf, n)) break;   // 上限超過でfalse
    //     w.commit(etag, epoch);                       // 失敗していたらfalseを返す
    class Writer {
        private:
            FsFile file;
            FixedString<PICO_PATH_LEN> final_path;
            FixedString<PICO_PATH_LEN> temp_path;
            FixedString<PICO_STR_M> host_;
            FixedString<PICO_STR_L> path_;

            uint32_t written_ = 0;
            uint32_t max_bytes_ = kMaxEntryBytes;
            bool open_ = false;
            bool failed_ = false;

        public:
            Writer() = default;
            ~Writer(){ this->abort(); }

            // コピーすると同じ一時ファイルを二重に閉じるので禁止する
            Writer(const Writer&) = delete;
            Writer& operator=(const Writer&) = delete;

            // 書き込みを開始する。親ディレクトリは必要に応じて作る。
            // max_bytesを0にすると既定(kMaxEntryBytes)を使う
            bool begin(const char* host, const char* path, uint32_t max_bytes = 0);

            // 受信したぶんを追記する。上限を超えるとfalseを返し、以降commit()は失敗する
            bool write(const void* data, size_t len);

            // 一時ファイルを本来の名前へ差し替え、目録を更新する。
            // 途中でwrite()が失敗していた場合は何も残さずfalseを返す
            bool commit(const char* validator, uint32_t fetched_epoch);

            // 書きかけを破棄する(一時ファイルを消す)。commit()済みなら何もしない
            void abort();

            bool isOpen() const { return open_; }
            bool hasFailed() const { return failed_; }
            uint32_t writtenBytes() const { return written_; }
            const char* tempPath() const { return temp_path.c_str(); }
            const char* finalPath() const { return final_path.c_str(); }
    };
}
