#pragma once

#include "net/Http_Response.hpp"
#include "task/Http_Get.hpp"
#include "storage/Doc_Cache.hpp"
#include "net/Manifest.hpp"
#include "util/Url.hpp"
#include "util/FixedString.hpp"

// 「URLを1本取ってきて、SD上の開けるパスにする」係。
//
// HttpGet(取得) と Doc_Cache(保存) を繋ぐだけの薄い層だが、ブラウザとして必要な
// 判断はここに集めてある:
//
//   - 手元にキャッシュがあれば検証子を添えて条件付きGETし、304ならそのまま使う
//   - 200なら一時ファイル経由でキャッシュを差し替えてから使う
//   - **取得に失敗しても、古いキャッシュがあればそれを開く**(圏外でも読める)
//   - マニフェストを渡されていて、そこのversionが手元の検証子と一致していれば
//     **何も聞かずにキャッシュを開く**(条件付きGETの往復すら省く)
//
// MarkdownViewへ渡すのは常にSD上のパスなので、View側はネットワークの存在を知らない。
class DocFetch {
    public:
        enum class State : uint8_t {
            Idle,
            Fetching,
            Ready,    // path() を開けばよい
            Failed,   // 開けるものが無い
        };

        // 結果がどこから来たか。フッタの出し分けに使う
        enum class Source : uint8_t {
            None,
            Network,          // 取ってきた
            NotModified,      // 304。キャッシュがそのまま使えた
            Manifest,         // マニフェストで最新と分かったので通信していない
            CacheAfterError,  // 取得に失敗したので古いキャッシュを開いた
        };

        // bypass_cache … 手元のキャッシュを無視して取り直す(リロードボタン)。
        // 検証子を送らないので304ではなく200が返り、キャッシュごと差し替わる。
        // 取得に失敗したときに古いキャッシュを開く挙動はそのまま残す
        // (取り直せなかっただけで、読めるものが手元にあるなら読ませたい)
        bool begin(const Url& url, bool bypass_cache = false);
        void update();
        void cancel();

        // このサーバのマニフェスト(SD上のパス)を教える。空文字/nullptrで忘れる。
        // **ホストごとの設定なので begin()/cancel() では消えない。**
        // 以降の begin() は、ここのversionと手元の検証子が一致する文書について
        // 通信せずに Ready(Source::Manifest)になる
        void setManifest(const char* cache_path);

        State state() const { return state_; }
        Source source() const { return source_; }

        // 開くべきSD上のパス(Ready のときだけ意味を持つ)
        const FixedString<PICO_PATH_LEN>& path() const { return cache_path; }
        // 失敗理由/注記。日本語でそのまま画面へ出せる
        const char* message() const { return message_; }

    private:
        // Doc_Cache::Writer をHTTPの本文の行き先として使うための中継。
        // Doc_Cacheをnet/へ依存させないよう、変換はここで閉じる
        class CacheSink : public IHttpSink {
            public:
                PICO_DocCache::Writer* writer = nullptr;
                bool write(const void* data, size_t len) override {
                    return writer ? writer->write(data, len) : false;
                }
        };

        HttpGet http;
        PICO_DocCache::Writer writer;
        CacheSink sink;

        Url url;
        FixedString<PICO_STR_M> host;          // "host" または "host:port"
        FixedString<PICO_PATH_LEN> cache_path;
        FixedString<PICO_PATH_LEN> manifest_path; // 空なら突き合わせをしない

        State state_ = State::Idle;
        Source source_ = Source::None;
        const char* message_ = "";

        // マニフェストと突き合わせて、通信せずに済むかを見る
        bool servableFromManifest(const char* validator) const;
        // 取得に失敗したときの逃げ道。古いキャッシュがあればそれを開く
        bool fallbackToCache(const char* why);
        void finish(State s, Source src, const char* msg);
};
