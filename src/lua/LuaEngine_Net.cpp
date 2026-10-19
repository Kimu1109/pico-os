// LuaEngine のWeb APIを使うための補助(pico.form_encode / pico.json_pick / pico.sd_json_pick)。
//
// json_pick は util/Json_Reader(SAX、確保なし)で流しながら読み、指定したパスの値だけをLuaへ渡す。
// pico.json_decode は文書全体をLuaの表にするので、SpotifyやTodoistのように「数KB〜数十KBの応答から
// 数個のキーだけ欲しい」ときにLuaの予算を大きく食う。こちらは結果の数個の値しかLuaに作らない。
// sd_json_pick は pico.http_request の save_to で保存した大きな応答(16KiB超)をSDから直接読む版。
// xml_pick / sd_xml_pick は同じことをXML(RSS/Atomなど)に対して行う(util/Xml_Reader)。

#include "lua/LuaEngine.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "util/Json_Reader.hpp"
#include "util/Xml_Reader.hpp"
#include "lua/LuaJson.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

namespace {
    constexpr int kMaxPickPaths = 32;
    constexpr size_t kPathBytes = 512;
    // sd_json_pick で読むファイルの上限(1回の呼び出しで画面を止める時間の頭打ち)
    constexpr size_t kMaxPickFileBytes = 1024u * 1024u;

    // 読みながら今いる場所のパス("item.artists.1.name"、配列は1始まり)を組み立て、
    // 頼まれたパスに当たった値を結果の表(Luaのスタックの result_idx)へ入れる。
    // Luaのuserdataの中に置く(OOMでlongjmpしてもGCが回収する。デストラクタは要らない作り)
    // json_pick / xml_pick で共通の、頼まれたパスと結果の表
    struct PickCommon {
        lua_State* L = nullptr;
        int result_idx = 0;
        int trunc_idx = 0;          // 切り詰めた値のパスを入れる表
        bool keep_null = false;     // null を pico.json_null で返す(JSONだけ。既定は入れない)

        int n_paths = 0;
        const char* paths[kMaxPickPaths] = {};
        size_t path_lens[kMaxPickPaths] = {};
        bool done[kMaxPickPaths] = {};
        int remaining = 0;          // まだ見つかっていないパスの数(XMLの "#" は数えない)
        bool has_count = false;     // 最後まで読まないと決まらないパス(XMLの "#")がある

        bool allDone() const { return remaining <= 0 && !has_count; }
        void afterPaths() {}

        // スタックの一番上の値を結果の表へ入れる
        void store(int i) {
            lua_setfield(L, result_idx, paths[i]);
            done[i] = true;
            remaining--;
        }
    };

    struct PickHandler : PickCommon, JsonReader::Handler {
        // bad = パスが長すぎて表せない(その中の値は当てない)
        struct Level { bool is_array; bool bad; int count; uint16_t path_len; };

        char path[kPathBytes] = {};
        size_t path_len = 0;
        Level levels[JsonReader::kMaxDepth + 1] = {};

        // levels[depth] の中の次の要素の名前を path へ足す(depth==0 は一番外の値で、名前は空)。
        // パスを表せなければ false
        bool enterChild(int depth, const char* key) {
            path_len = (depth >= 1) ? levels[depth].path_len : 0;
            path[path_len] = '\0';
            if (depth < 1) return true;
            Level& p = levels[depth];
            p.count++;
            if (p.bad) return false;
            char idx[16];
            const char* comp = key;
            if (p.is_array || !key) {
                snprintf(idx, sizeof(idx), "%d", p.count);
                comp = idx;
            }
            const size_t cl = strlen(comp);
            const size_t sep = (path_len > 0) ? 1 : 0;
            if (path_len + sep + cl + 2 >= kPathBytes) return false;
            if (sep) path[path_len++] = '.';
            memcpy(path + path_len, comp, cl);
            path_len += cl;
            path[path_len] = '\0';
            return true;
        }

        int find(const char* p, size_t len) const {
            for (int i = 0; i < n_paths; i++) {
                if (!done[i] && path_lens[i] == len && memcmp(paths[i], p, len) == 0) return i;
            }
            return -1;
        }

        void onBegin(const char* key, bool is_array, int depth) override {
            if (depth <= 0 || depth > JsonReader::kMaxDepth) return;
            const bool ok = enterChild(depth - 1, key);
            levels[depth].bad = !ok;
            levels[depth].is_array = is_array;
            levels[depth].count = 0;
            levels[depth].path_len = (uint16_t)path_len;
        }

        void onEnd(bool is_array, int depth) override {
            (void)is_array;
            if (depth <= 0 || depth > JsonReader::kMaxDepth || remaining <= 0) return;
            // "パス#" は配列/オブジェクトの要素の数
            const size_t base = levels[depth].path_len;
            if (levels[depth].bad || base + 2 >= kPathBytes) return;
            path[base] = '#';
            path[base + 1] = '\0';
            const int i = find(path, base + 1);
            path[base] = '\0';
            if (i >= 0) {
                lua_pushinteger(L, levels[depth].count);
                store(i);
            }
        }

        void onValue(const char* key, JsonReader::Type type, const char* text, size_t len,
                     bool truncated, int depth) override {
            if (remaining <= 0) return;
            if (!enterChild(depth, key)) return;
            const int i = find(path, path_len);
            if (i < 0) return;
            switch (type) {
                case JsonReader::Type::String:
                    lua_pushlstring(L, text, len);
                    if (truncated && trunc_idx) {
                        lua_pushboolean(L, 1);
                        lua_setfield(L, trunc_idx, paths[i]);
                    }
                    break;
                case JsonReader::Type::Number:
                    if (lua_stringtonumber(L, text) == 0) lua_pushlstring(L, text, len);
                    break;
                case JsonReader::Type::True:  lua_pushboolean(L, 1); break;
                case JsonReader::Type::False: lua_pushboolean(L, 0); break;
                case JsonReader::Type::Null:
                    if (!keep_null) { done[i] = true; remaining--; return; }
                    lua_pushlightuserdata(L, LuaJson::NullValue());
                    break;
            }
            store(i);
        }
    };

    struct PickWork {
        PickHandler h;
        JsonReader reader;
    };

    // XMLのパス: 要素名を "." でつなぐ("rss.channel.item.title")。同じ名前の兄弟は先頭が1番で、
    // 名前の直後に番号を置くと選べる("rss.channel.item.2.title")。番号を省くと1番目。
    // 属性は "要素のパス@属性名"、同じ名前の子の数は "親のパス.名前#"。
    // 同じ要素の中の文字が子要素で何度か区切られているときは、最初のひとまとまりを返す。
    struct XmlPickHandler : PickCommon, XmlReader::Handler {
        static constexpr int kKids = 16;
        struct Kid { uint32_t hash; uint16_t count; };
        struct Level {
            char name[XmlReader::kNameBytes];
            uint16_t idx;           // 同じ名前の兄弟の何番目か(1始まり)
            uint8_t n_kids;
            Kid kids[kKids];        // 子の名前ごとの数(溢れた名前は常に1番目として扱う)
        };
        enum Kind : uint8_t { Text, Attr, Count };

        Level levels[XmlReader::kMaxDepth + 1] = {};   // 0 は文書そのもの(子はルート要素)
        uint8_t kind[kMaxPickPaths] = {};
        size_t elem_len[kMaxPickPaths] = {};           // 要素の部分の長さ("@" や "#" の手前まで)
        const char* attr_name[kMaxPickPaths] = {};     // Attr のとき "@" の後ろ

        void afterPaths() { classify(); }

        static uint32_t Hash(const char* s) {
            uint32_t h = 2166136261u;
            while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
            return h;
        }

        // 頼まれたパスを分解して覚える(Lua文字列の中を指すだけ)
        void classify() {
            for (int i = 0; i < n_paths; i++) {
                const char* p = paths[i];
                const size_t n = path_lens[i];
                kind[i] = Text;
                elem_len[i] = n;
                attr_name[i] = nullptr;
                const char* at = (const char*)memchr(p, '@', n);
                if (at) {
                    kind[i] = Attr;
                    elem_len[i] = (size_t)(at - p);
                    attr_name[i] = at + 1;
                } else if (p[n - 1] == '#') {
                    kind[i] = Count;
                    elem_len[i] = n - 1;
                    has_count = true;
                    remaining--;     // 最後まで読むので「見つかった」の数には入れない
                }
            }
        }

        // levels[1..depth] の道筋が req(len バイト)と同じか。any_last: 最後の要素は何番目でもよい(番号も付けられない)
        bool match(const char* req, size_t len, int depth, bool any_last) const {
            const char* p = req;
            const char* end = req + len;
            for (int d = 1; d <= depth; d++) {
                const size_t nl = strlen(levels[d].name);
                if ((size_t)(end - p) < nl || memcmp(p, levels[d].name, nl) != 0) return false;
                p += nl;
                if (p < end && *p != '.') return false;
                const bool last = (d == depth);
                // 名前の後ろの番号
                const char* q = p;
                bool has_idx = false;
                int num = 0;
                if (q < end && *q == '.') {
                    const char* r = q + 1;
                    while (r < end && *r >= '0' && *r <= '9' && num < 100000) { num = num * 10 + (*r - '0'); r++; }
                    if (r > q + 1 && (r == end || *r == '.')) { has_idx = true; q = r; }
                }
                if (has_idx) {
                    if (last && any_last) return false;
                    if (num != levels[d].idx) return false;
                    p = q;
                } else if (!(last && any_last) && levels[d].idx != 1) {
                    return false;
                }
                if (last) return p == end;
                if (p >= end || *p != '.') return false;
                p++;
            }
            return false;
        }

        void onOpen(const char* name, int depth) override {
            if (depth < 1 || depth > XmlReader::kMaxDepth) return;
            Level& parent = levels[depth - 1];
            Level& me = levels[depth];
            strncpy(me.name, name, sizeof(me.name) - 1);
            me.name[sizeof(me.name) - 1] = '\0';
            me.n_kids = 0;
            // 同じ名前の兄弟の何番目か
            const uint32_t h = Hash(name);
            me.idx = 1;
            bool found = false;
            for (uint8_t k = 0; k < parent.n_kids; k++) {
                if (parent.kids[k].hash == h) { me.idx = ++parent.kids[k].count; found = true; break; }
            }
            if (!found && parent.n_kids < kKids) parent.kids[parent.n_kids++] = Kid{h, 1};
            // 数を頼まれていれば更新する(最後に読んだ値が残る)
            if (has_count) {
                for (int i = 0; i < n_paths; i++) {
                    if (kind[i] == Count && match(paths[i], elem_len[i], depth, true)) {
                        lua_pushinteger(L, me.idx);
                        lua_setfield(L, result_idx, paths[i]);
                    }
                }
            }
        }

        void onAttribute(const char* name, const char* value, size_t len, bool truncated, int depth) override {
            if (remaining <= 0 || depth < 1 || depth > XmlReader::kMaxDepth) return;
            for (int i = 0; i < n_paths; i++) {
                if (done[i] || kind[i] != Attr || strcmp(attr_name[i], name) != 0) continue;
                if (!match(paths[i], elem_len[i], depth, false)) continue;
                lua_pushlstring(L, value, len);
                if (truncated && trunc_idx) {
                    lua_pushboolean(L, 1);
                    lua_setfield(L, trunc_idx, paths[i]);
                }
                store(i);
            }
        }

        void onText(const char* name, const char* text, size_t len, bool truncated, int depth) override {
            (void)name;
            if (remaining <= 0 || depth < 1 || depth > XmlReader::kMaxDepth) return;
            // 同じ要素を指す書き方(番号あり・なし)が両方頼まれていれば、全部に入れる
            for (int i = 0; i < n_paths; i++) {
                if (done[i] || kind[i] != Text || !match(paths[i], elem_len[i], depth, false)) continue;
                lua_pushlstring(L, text, len);
                if (truncated && trunc_idx) {
                    lua_pushboolean(L, 1);
                    lua_setfield(L, trunc_idx, paths[i]);
                }
                store(i);
            }
        }
    };

    struct XmlPickWork {
        XmlPickHandler h;
        XmlReader reader;
    };
}

struct LuaEngineNet {
    static LuaEngine* Self(lua_State* L) {
        return *static_cast<LuaEngine**>(lua_getextraspace(L));   // registerFn() の説明参照
    }

    // ---- pico.form_encode(tbl) -> "a=1&b=x+y" ----
    // application/x-www-form-urlencoded。キーは名前順(結果を決まった形にするため)。
    // 値は文字列/数値/真偽値、または同じキーを繰り返す配列({scope = {"a", "b"}} → scope=a&scope=b)
    static void addEncoded(std::string& out, const char* s, size_t n) {
        static const char* hex = "0123456789ABCDEF";
        for (size_t i = 0; i < n; i++) {
            const unsigned char c = (unsigned char)s[i];
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '-' || c == '_' || c == '.' || c == '*') {
                out += (char)c;
            } else if (c == ' ') {
                out += '+';
            } else {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
    }

    static bool isScalar(int t) { return t == LUA_TSTRING || t == LUA_TNUMBER || t == LUA_TBOOLEAN; }

    // スタックの idx の値を out へエンコードして足す(文字列/数値/真偽値であることは確かめてある)
    static void appendValue(lua_State* L, int idx, std::string& out) {
        if (lua_type(L, idx) == LUA_TBOOLEAN) {
            out += lua_toboolean(L, idx) ? "true" : "false";
            return;
        }
        lua_pushvalue(L, idx);               // 数値はコピーを文字列にする(元の値を変えない)
        size_t n = 0;
        const char* v = lua_tolstring(L, -1, &n);
        addEncoded(out, v, n);
        lua_pop(L, 1);
    }

    static int l_form_encode(lua_State* L) {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_settop(L, 1);
        // 1周目: 形を確かめ、キーを集める(ここでの誤りはまだ何も確保していないので漏れない)
        lua_newtable(L);                      // 2: キーの配列
        int nk = 0;
        lua_pushnil(L);
        while (lua_next(L, 1) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING) return luaL_error(L, "pico.form_encode: キーは文字列で指定してください");
            const char* key = lua_tostring(L, -2);
            const int vt = lua_type(L, -1);
            if (vt == LUA_TTABLE) {
                const lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
                for (lua_Integer k = 1; k <= n; k++) {
                    const int et = lua_rawgeti(L, -1, k);
                    lua_pop(L, 1);
                    if (!isScalar(et)) {
                        return luaL_error(L, "pico.form_encode: '%s' の配列の値は文字列・数値・真偽値で指定してください", key);
                    }
                }
            } else if (!isScalar(vt)) {
                return luaL_error(L, "pico.form_encode: '%s' の値は文字列・数値・真偽値(またはその配列)で指定してください", key);
            }
            lua_pop(L, 1);
            lua_pushvalue(L, -1);
            lua_rawseti(L, 2, ++nk);
        }
        // 名前順に(キーの数は小さいので挿入ソート)
        for (int i = 2; i <= nk; i++) {
            for (int j = i; j > 1; j--) {
                lua_rawgeti(L, 2, j - 1);
                lua_rawgeti(L, 2, j);
                if (strcmp(lua_tostring(L, -2), lua_tostring(L, -1)) <= 0) { lua_pop(L, 2); break; }
                lua_rawseti(L, 2, j - 1);
                lua_rawseti(L, 2, j);
            }
        }
        // 2周目: 組み立てる
        std::string out;
        for (int i = 1; i <= nk; i++) {
            lua_rawgeti(L, 2, i);             // 3: キー
            size_t kl = 0;
            const char* key = lua_tolstring(L, 3, &kl);
            lua_pushvalue(L, 3);
            lua_rawget(L, 1);                 // 4: 値
            auto pair = [&](int vidx) {
                if (!out.empty()) out += '&';
                addEncoded(out, key, kl);
                out += '=';
                appendValue(L, vidx, out);
            };
            if (lua_type(L, 4) == LUA_TTABLE) {
                const lua_Integer n = (lua_Integer)lua_rawlen(L, 4);
                for (lua_Integer k = 1; k <= n; k++) {
                    lua_rawgeti(L, 4, k);
                    pair(5);
                    lua_pop(L, 1);
                }
            } else {
                pair(4);
            }
            lua_settop(L, 2);
        }
        lua_pushlstring(L, out.data(), out.size());
        return 1;
    }

    // ---- json_pick の共通部分 ----
    // スタック: paths_idx = パスの配列、opts_idx = opts(無ければ0)。
    // 結果の表と切り詰めの表を積み、読み取りの準備をした PickWork を返す
    template <typename Work>
    static Work* beginPick(lua_State* L, int paths_idx, int opts_idx, const char* fn) {
        luaL_checktype(L, paths_idx, LUA_TTABLE);
        bool keep_null = false;
        if (opts_idx && !lua_isnoneornil(L, opts_idx)) {
            luaL_checktype(L, opts_idx, LUA_TTABLE);
            lua_getfield(L, opts_idx, "keep_null");
            keep_null = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }
        const lua_Integer n = (lua_Integer)lua_rawlen(L, paths_idx);
        if (n < 1) luaL_error(L, "%s: パスを1つ以上指定してください", fn);
        if (n > kMaxPickPaths) luaL_error(L, "%s: パスは%d個までです", fn, kMaxPickPaths);
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, paths_idx, i);
            if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "%s: パスは文字列で指定してください", fn);
            size_t pl = 0;
            lua_tolstring(L, -1, &pl);
            if (pl == 0 || pl + 2 >= kPathBytes) luaL_error(L, "%s: パスが空か長すぎます", fn);
            lua_pop(L, 1);
        }

        void* mem = lua_newuserdatauv(L, sizeof(Work), 0);   // GCが回収する
        Work* w = new (mem) Work();
        lua_newtable(L);                                          // 結果
        w->h.result_idx = lua_gettop(L);
        lua_newtable(L);                                          // 切り詰めた値のパス
        w->h.trunc_idx = lua_gettop(L);
        w->h.L = L;
        w->h.keep_null = keep_null;
        // パスの文字列はパスの配列が持ち続けるので、ポインタだけ覚える
        w->h.n_paths = (int)n;
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, paths_idx, i);
            w->h.paths[i - 1] = lua_tolstring(L, -1, &w->h.path_lens[i - 1]);
            lua_pop(L, 1);
        }
        w->h.remaining = (int)n;
        w->h.afterPaths();
        w->reader.reset(&w->h);
        return w;
    }

    // 結果を返す: 成功なら (結果, 切り詰めたパスの表|nil)、誤りなら (nil, 理由)
    template <typename Work>
    static int finishPick(lua_State* L, Work* w, bool ok, const char* err) {
        if (!ok) {
            lua_pushnil(L);
            lua_pushstring(L, err);
            return 2;
        }
        lua_pushvalue(L, w->h.result_idx);
        lua_pushnil(L);
        if (lua_next(L, w->h.trunc_idx) != 0) {
            lua_pop(L, 2);
            lua_pushvalue(L, w->h.trunc_idx);
        } else {
            lua_pushnil(L);
        }
        return 2;
    }

    // pico.json_pick(text, paths [, opts]) -> 結果, 切り詰めたパス | nil, 理由
    static int l_json_pick(lua_State* L) {
        size_t len = 0;
        const char* text = luaL_checklstring(L, 1, &len);
        PickWork* w = beginPick<PickWork>(L, 2, 3, "pico.json_pick");
        bool ok = w->reader.feed(text, len);
        const char* err = w->reader.error();
        if (ok && !w->reader.complete()) {
            // 一番外の値が終わっていない(切れた応答)。頼んだものが全部見つかっていれば許す
            ok = w->h.allDone();
            err = "JSONが途中で切れています";
        }
        return finishPick(L, w, ok, err);
    }

    // pico.sd_json_pick(path, paths [, opts]) -> 同上。SDのファイルを流しながら読む(1MiBまで)
    static int l_sd_json_pick(lua_State* L) {
        LuaEngine* self = Self(L);
        const char* file = luaL_checkstring(L, 1);
        PickWork* w = beginPick<PickWork>(L, 2, 3, "pico.sd_json_pick");
        if (!OSData::SD_usable) return finishPick(L, w, false, "SDカードが使えません");
        if (!self->SdPathAllowed(file)) {
            LOG_APP_WARN("pico.sd_json_pick: アプリディレクトリ外へのアクセスは許可されていません: %s", file);
            return finishPick(L, w, false, "このパスは読めません(権限)");
        }
        FsFile f = OSData::SD.open(file, O_RDONLY);
        if (!f) return finishPick(L, w, false, "ファイルを開けません");
        if ((size_t)f.fileSize() > kMaxPickFileBytes) {
            f.close();
            return finishPick(L, w, false, "ファイルが大きすぎます(1MiBまで)");
        }
        bool ok = true;
        const char* err = "";
        char chunk[256];
        while (ok) {
            const int got = f.read((uint8_t*)chunk, sizeof(chunk));
            if (got <= 0) break;
            ok = w->reader.feed(chunk, (size_t)got);
            if (!ok) err = w->reader.error();
            // 頼んだものが全部見つかれば残りは読まない
            if (w->h.allDone()) break;
        }
        f.close();
        if (ok && !w->h.allDone() && !w->reader.complete()) {
            ok = false;
            err = "JSONが途中で切れています";
        }
        return finishPick(L, w, ok, err);
    }

    // pico.xml_pick(text, paths [, opts]) -> 結果, 切り詰めたパス | nil, 理由
    static int l_xml_pick(lua_State* L) {
        size_t len = 0;
        const char* text = luaL_checklstring(L, 1, &len);
        XmlPickWork* w = beginPick<XmlPickWork>(L, 2, 3, "pico.xml_pick");
        bool ok = w->reader.feed(text, len);
        const char* err = w->reader.error();
        if (ok && !w->reader.complete()) {
            ok = w->h.allDone();
            err = "XMLが途中で切れています";
        }
        return finishPick(L, w, ok, err);
    }

    // pico.sd_xml_pick(path, paths [, opts]) -> 同上。SDのファイルを流しながら読む(1MiBまで)
    static int l_sd_xml_pick(lua_State* L) {
        LuaEngine* self = Self(L);
        const char* file = luaL_checkstring(L, 1);
        XmlPickWork* w = beginPick<XmlPickWork>(L, 2, 3, "pico.sd_xml_pick");
        if (!OSData::SD_usable) return finishPick(L, w, false, "SDカードが使えません");
        if (!self->SdPathAllowed(file)) {
            LOG_APP_WARN("pico.sd_xml_pick: アプリディレクトリ外へのアクセスは許可されていません: %s", file);
            return finishPick(L, w, false, "このパスは読めません(権限)");
        }
        FsFile f = OSData::SD.open(file, O_RDONLY);
        if (!f) return finishPick(L, w, false, "ファイルを開けません");
        if ((size_t)f.fileSize() > kMaxPickFileBytes) {
            f.close();
            return finishPick(L, w, false, "ファイルが大きすぎます(1MiBまで)");
        }
        bool ok = true;
        const char* err = "";
        char chunk[256];
        while (ok) {
            const int got = f.read((uint8_t*)chunk, sizeof(chunk));
            if (got <= 0) break;
            ok = w->reader.feed(chunk, (size_t)got);
            if (!ok) err = w->reader.error();
            if (w->h.allDone()) break;
        }
        f.close();
        if (ok && !w->h.allDone() && !w->reader.complete()) {
            ok = false;
            err = "XMLが途中で切れています";
        }
        return finishPick(L, w, ok, err);
    }
};

void LuaEngine::RegisterNetApi() {
    registerFn("form_encode", LuaEngineNet::l_form_encode);
    registerFn("json_pick", LuaEngineNet::l_json_pick);
    registerFn("sd_json_pick", LuaEngineNet::l_sd_json_pick);
    registerFn("xml_pick", LuaEngineNet::l_xml_pick);
    registerFn("sd_xml_pick", LuaEngineNet::l_sd_xml_pick);
}
