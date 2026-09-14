#pragma once

#include "util/FixedString.hpp"
#include "storage/SD_IO.hpp"
#include "consts.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// HTTPのURLを分解して持つ。
//
// "http://" を各所で strncmp する形にしないための型。scheme をここに閉じ込めておけば、
// 将来HTTPS対応を入れるときに触るのは Http_Get 側の接続処理だけで済む(PROTOCOL.md)。
//
// パスとクエリは分けて持つ。相対解決(PICO_IO::resolve)はパスにしか効かず、
// クエリの中の "/" まで畳んでしまうと壊れるため。
struct Url {
    FixedString<PICO_STR_M> host;  // ポートを含まないホスト名
    uint16_t port = 80;
    bool secure = false;           // https かどうか。現状 Http_Get は false のみ受け付ける

    FixedString<PICO_STR_L> path;  // "/" 始まり
    //"?" を含まない。空なら無し。パスより長いのは、検索語を
    //パーセントエンコードすると日本語1文字が9バイトになるため
    FixedString<PICO_STR_LL> query;

    bool empty() const { return host.empty(); }
};

namespace UrlTools {

    // "http://host[:port][/path][?query]" を分解する。
    // scheme が無い/未知の場合は false(相対参照は Resolve() の担当)。
    inline bool Parse(Url& out, const char* url)
    {
        if (!url) return false;

        bool secure = false;
        const char* p = nullptr;

        if (strncmp(url, "http://", 7) == 0) {
            p = url + 7;
        } else if (strncmp(url, "https://", 8) == 0) {
            p = url + 8;
            secure = true;
        } else {
            return false;
        }

        out = Url{};
        out.secure = secure;
        out.port = secure ? 443 : 80;

        // ---- ホスト(と任意のポート) ----
        const char* hostStart = p;
        while (*p && *p != '/' && *p != '?' && *p != ':') p++;
        if (p == hostStart) return false; // ホストが空
        if (!out.host.assign(hostStart, (size_t)(p - hostStart))) return false;

        if (*p == ':') {
            p++;
            const char* portStart = p;
            while (*p >= '0' && *p <= '9') p++;
            if (p == portStart) return false; // ':' の後に数字が無い

            const long value = strtol(portStart, nullptr, 10);
            if (value <= 0 || value > 65535) return false;
            out.port = (uint16_t)value;
        }

        // ---- パスとクエリ ----
        const char* queryStart = nullptr;
        const char* pathStart = p;

        while (*p && *p != '?') p++;
        const size_t pathLen = (size_t)(p - pathStart);
        if (*p == '?') queryStart = p + 1;

        if (pathLen == 0) {
            if (!out.path.assign("/")) return false;
        } else {
            FixedString<PICO_STR_L> raw;
            if (!raw.assign(pathStart, pathLen)) return false;
            //"." や ".." を畳んでおく。以降どこでも正規形として扱える
            FixedString<PICO_PATH_LEN> normalized;
            if (!PICO_IO::normalize(normalized, raw.c_str())) return false;
            if (!out.path.assign(normalized.c_str())) return false;
        }

        if (queryStart && *queryStart != '\0') {
            if (!out.query.assign(queryStart)) return false;
        }

        return true;
    }

    // base から見た参照 ref を解決する(一般的なブラウザと同じ規則)。
    //   "http://other/x.md" … 絶対URL。ホストごと移る
    //   "/docs/x.md"        … 同じホストのルート基準
    //   "../x.md"           … base.path のあるディレクトリ基準
    inline bool Resolve(Url& out, const Url& base, const char* ref)
    {
        if (!ref || ref[0] == '\0') return false;

        // 絶対URLならホストごと差し替え
        if (strncmp(ref, "http://", 7) == 0 || strncmp(ref, "https://", 8) == 0) {
            return Parse(out, ref);
        }

        // クエリは相対解決の対象外なので、先に切り離す
        FixedString<PICO_STR_L> refPath;
        FixedString<PICO_STR_LL> refQuery;
        const char* q = strchr(ref, '?');
        if (q) {
            if (!refPath.assign(ref, (size_t)(q - ref))) return false;
            if (q[1] != '\0' && !refQuery.assign(q + 1)) return false;
        } else {
            if (!refPath.assign(ref)) return false;
        }

        //"?q=..." だけの参照はパスを据え置いてクエリだけ差し替える
        if (refPath.empty()) {
            out = base;
            return out.query.assign(refQuery);
        }

        FixedString<PICO_PATH_LEN> resolved;
        if (!PICO_IO::resolve(resolved, base.path.c_str(), refPath.c_str())) return false;

        out = base;
        if (!out.path.assign(resolved.c_str())) return false;
        return out.query.assign(refQuery);
    }

    // リクエストラインへ書く形("/path" または "/path?query")
    inline bool RequestTarget(FixedString<PICO_STR_256B>& out, const Url& url)
    {
        if (!out.assign(url.path)) return false;
        if (url.query.empty()) return true;
        if (!out.append("?")) return false;
        return out.append(url.query);
    }

    // Hostヘッダへ書く形。既定ポートのときは省く(HTTPの慣習)
    inline bool HostHeader(FixedString<PICO_STR_M>& out, const Url& url)
    {
        if (!out.assign(url.host)) return false;

        const uint16_t defaultPort = url.secure ? 443 : 80;
        if (url.port == defaultPort) return true;

        char buf[8];
        snprintf(buf, sizeof(buf), ":%u", (unsigned)url.port);
        return out.append(buf);
    }

    // 履歴などへ保存して後で Parse() し直せる完全な形("http://host:port/path?query")
    template<size_t N>
    inline bool FormatFull(FixedString<N>& out, const Url& url)
    {
        FixedString<PICO_STR_M> hostPart;
        if (!HostHeader(hostPart, url)) return false;

        if (!out.assign(url.secure ? "https://" : "http://")) return false;
        if (!out.append(hostPart)) return false;
        if (!out.append(url.path)) return false;
        if (url.query.empty()) return true;
        if (!out.append("?")) return false;
        return out.append(url.query);
    }

    // クエリの値をパーセントエンコードする。
    // 検索語は日本語なので、そのままではリクエストラインへ書けない
    // (PROTOCOL.md「3. 検索」の `q` はURLエンコードされたUTF-8)。
    // 無変換で通すのは RFC3986 の unreserved(A-Za-z0-9 と - . _ ~)だけにしてある。
    template<size_t N>
    inline bool EncodeComponent(FixedString<N>& out, const char* text)
    {
        out.clear();
        if (!text) return true;

        for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
            const unsigned char c = *p;
            const bool unreserved =
                (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '.' || c == '_' || c == '~';

            if (unreserved) {
                if (!out.append((char)c)) return false;
            } else {
                char escaped[4];
                snprintf(escaped, sizeof(escaped), "%%%02X", (unsigned)c);
                if (!out.append(escaped)) return false;
            }
        }
        return true;
    }

    // 表示・ログ用。キャッシュのキーにも使えるよう scheme は含めない
    // (http時代のキャッシュがhttps移行後も生きるように。PROTOCOL.md参照)
    inline bool Format(FixedString<PICO_PATH_LEN>& out, const Url& url)
    {
        FixedString<PICO_STR_M> hostPart;
        if (!HostHeader(hostPart, url)) return false;

        if (!out.assign(hostPart)) return false;
        if (!out.append(url.path)) return false;
        if (url.query.empty()) return true;
        if (!out.append("?")) return false;
        return out.append(url.query);
    }
}
