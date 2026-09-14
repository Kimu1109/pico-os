#pragma once

#include "net/Http_Response.hpp"
#include "task/Http_Get.hpp"
#include "storage/Doc_Cache.hpp"
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
            CacheAfterError,  // 取得に失敗したので古いキャッシュを開いた
        };

        bool begin(const Url& url);
        void update();
        void cancel();

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

        State state_ = State::Idle;
        Source source_ = Source::None;
        const char* message_ = "";

        // 取得に失敗したときの逃げ道。古いキャッシュがあればそれを開く
        bool fallbackToCache(const char* why);
        void finish(State s, Source src, const char* msg);
};
