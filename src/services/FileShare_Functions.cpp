#include "services/FileShare_Functions.hpp"
#include "services/Lock_Functions.hpp"
#include "hal/Network_Functions.hpp"
#include "net/File_Share.hpp"
#include "storage/SD_IO.hpp"
#include "crypto/Secret_Aead.hpp"
#include "system/Log_Functions.hpp"
#include "OS_Data.hpp"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
    #include <errno.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#elif !defined(PICOOS_PC)
    #include <WiFi.h>
#endif

namespace FileShareFunctions {
    bool (*path_busy)(const char* path) = nullptr;
    void (*sd_changed)() = nullptr;
    void (*file_changed)(const char* path) = nullptr;
}

namespace {
    using namespace FileShareFunctions;
    using FileShare::Route;

    State state = State::Idle;
    char share_key[FileShare::kKeyLen + 1] = "";
    Activity current;
    FixedString<PICO_STR_LL> recent[kRecentCount];
    int recent_count = 0;
    uint32_t revision = 0;
    bool modified = false;   // 何か書き換えた(Stop() で sd_changed() を呼ぶ)
    bool background = false; // 画面を閉じても続けている(SetBackground)
    FileShare::Throttle throttle;
    bool direct = false;     // 本体がAPになっている(直接つなぐ)
    char ap_ssid[33] = "";   // SSIDは32バイトまで
    char ap_password[12] = "";

    constexpr uint32_t kDrainMs = 2000;  // 断った要求の本文を読み捨てて待つ上限(読まずに閉じると相手に応答が届かない)

    void AddRecent(const char* fmt, ...){
        for(int i = kRecentCount - 1; i > 0; i--) recent[i].assign(recent[i - 1]);
        char buf[PICO_STR_LL];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        recent[0].assign(buf);
        if(recent_count < kRecentCount) recent_count++;
        revision++;
    }

    void FormatSize(uint64_t n, char* out, size_t cap){
        if(n < 1024) snprintf(out, cap, "%uB", (unsigned)n);
        else if(n < 1024u * 1024u) snprintf(out, cap, "%.1fKB", (double)n / 1024.0);
        else snprintf(out, cap, "%.1fMB", (double)n / (1024.0 * 1024.0));
    }

    // ---- 接続(実機は WiFiServer、PCは生のソケット) ----
    // 書き込みは送れる分だけ送って戻る(ループを止めない)。read: 読めたバイト数、0=まだ、-1=閉じた。
    // write: 送れたバイト数、0=今は送れない、-1=閉じた
    struct Conn {
#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
        int listen_fd = -1;
        int client_fd = -1;
#elif !defined(PICOOS_PC)
        WiFiServer http{kHttpPort};
        WiFiClient client;
#endif
        bool begin(FixedString<PICO_STR_LL>& err);
        void end();
        bool accept();
        void drop();
        int read(uint8_t* p, size_t n);
        int write(const uint8_t* p, size_t n);
    };

#if defined(PICOOS_PC) && !defined(__EMSCRIPTEN__)
    uint16_t PcPort(){
        const char* env = getenv("PICOOS_SHARE_PORT");
        const int v = env ? atoi(env) : 0;
        return (v > 0 && v < 65536) ? (uint16_t)v : 8082;
    }
    uint16_t ListenPort(){ return PcPort(); }
    bool Conn::begin(FixedString<PICO_STR_LL>& err){
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if(listen_fd < 0){ err.assign("待ち受けを始められません"); return false; }
        const int one = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 母艦の外からは開けないようにする
        addr.sin_port = htons(PcPort());
        if(::bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(listen_fd, 4) != 0){
            ::close(listen_fd);
            listen_fd = -1;
            err.assign("待ち受けの番号(ポート)が使えません");
            return false;
        }
        ::fcntl(listen_fd, F_SETFL, ::fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK);
        return true;
    }
    void Conn::end(){
        drop();
        if(listen_fd >= 0) ::close(listen_fd);
        listen_fd = -1;
    }
    bool Conn::accept(){
        const int fd = ::accept(listen_fd, nullptr, nullptr);
        if(fd < 0) return false;
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        client_fd = fd;
        return true;
    }
    void Conn::drop(){
        if(client_fd >= 0) ::close(client_fd);
        client_fd = -1;
    }
    int Conn::read(uint8_t* p, size_t n){
        const ssize_t r = ::recv(client_fd, p, n, MSG_DONTWAIT);
        if(r > 0) return (int)r;
        if(r == 0) return -1;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
    int Conn::write(const uint8_t* p, size_t n){
        const ssize_t w = ::send(client_fd, p, n, MSG_DONTWAIT | MSG_NOSIGNAL);
        if(w >= 0) return (int)w;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
    }
#elif !defined(PICOOS_PC)
    uint16_t ListenPort(){ return kHttpPort; }
    bool Conn::begin(FixedString<PICO_STR_LL>&){
        http.begin();
        return true;
    }
    void Conn::end(){
        drop();
        http.stop();
    }
    bool Conn::accept(){
        WiFiClient c = http.accept();
        if(!c) return false;
        client = c;
        return true;
    }
    void Conn::drop(){ if(client) client.stop(); }
    int Conn::read(uint8_t* p, size_t n){
        const int avail = client.available();
        if(avail <= 0) return client.connected() ? 0 : -1;
        const int r = client.read(p, (size_t)avail < n ? (size_t)avail : n);
        return r < 0 ? -1 : r;
    }
    int Conn::write(const uint8_t* p, size_t n){
        if(!client.connected()) return -1;
        // 送り手の窓に入る分だけ渡す(それより多く渡すと WiFiClient::write() が空くまで待つ)
        const int room = client.availableForWrite();
        if(room <= 0) return 0;
        return (int)client.write(p, (size_t)room < n ? (size_t)room : n);
    }
#else // Web
    uint16_t ListenPort(){ return 0; }
    bool Conn::begin(FixedString<PICO_STR_LL>& err){ err.assign("Web版では使えません"); return false; }
    void Conn::end(){}
    bool Conn::accept(){ return false; }
    void Conn::drop(){}
    int Conn::read(uint8_t*, size_t){ return -1; }
    int Conn::write(const uint8_t*, size_t){ return -1; }
#endif

    // ---- 1本の接続の進み ----
    enum class Phase : uint8_t { Head, Receive, Send, Drain };
    enum class Body : uint8_t { None, Memory, File, Listing };

    // 持ち物はまとめて Start() で new する(常に持つと約9KBになるため)
    struct ShareServer {
        Conn conn;
        FileShare::RequestHead head;
        uint8_t io[kIoBytes];
        size_t out_pos = 0, out_len = 0;   // io[out_pos, out_len) をまだ送っていない
        bool has_client = false;
        Phase phase = Phase::Head;
        uint32_t last_progress = 0;
        uint32_t drain_since = 0;
        uint64_t unread = 0;               // 要求の本文のうち、まだ読んでいないバイト数
        Body body = Body::None;
        const char* mem = nullptr;
        size_t mem_len = 0, mem_pos = 0;
        FsFile file;                       // ダウンロード・アップロード・一覧のフォルダ
        uint64_t remaining = 0;            // アップロードの残り / ダウンロードの残り
        bool listing_root = false;
        FixedString<PICO_PATH_LEN> path;   // アップロードの行き先
        FixedString<PICO_PATH_LEN> part;   // その書きかけ(<path>.part)
    };
    ShareServer* server = nullptr;

    bool Busy(const char* p){ return path_busy && path_busy(p); }
    void Changed(const char* path){ if(file_changed) file_changed(path); }

    bool IsDir(const char* p){
        FsFile f = OSData::SD.open(p);
        const bool d = f && f.isDir();
        f.close();
        return d;
    }
    bool ParentIsDir(const char* p){
        FixedString<PICO_PATH_LEN> par;
        if(!PICO_IO::parent(par, p)) return false;
        return IsDir(par.c_str());
    }

    // ---- 応答を組む ----
    void BeginResponse(int status, const char* ctype, int64_t length, const char* extra){
        ShareServer& s = *server;
        FileShare::FormatHeader(status, ctype, length, extra, (char*)s.io, sizeof(s.io));
        s.out_pos = 0;
        s.out_len = strlen((const char*)s.io);
        s.body = Body::None;
        s.phase = Phase::Send;
    }
    // 短い本文(説明の文・JSON)をそのまま付けて返す
    void Respond(int status, const char* ctype, const char* text){
        ShareServer& s = *server;
        const size_t tl = text ? strlen(text) : 0;
        BeginResponse(status, ctype, (int64_t)tl, nullptr);
        if(tl > 0 && s.out_len + tl <= sizeof(s.io)){
            memcpy(s.io + s.out_len, text, tl);
            s.out_len += tl;
        }
    }
    void RespondText(int status, const char* text){ Respond(status, "text/plain; charset=utf-8", text); }

    void RespondPathError(FileShare::PathCheck c){
        if(c == FileShare::PathCheck::Forbidden) RespondText(403, "/sys には触れません");
        else RespondText(400, "使えない名前です");
    }
    // query の key をパスとして読む
    bool QueryPath(const char* key, FixedString<PICO_PATH_LEN>& out){
        char raw[PICO_PATH_LEN + 1];
        if(!FileShare::QueryValue(server->head.query(), key, raw, sizeof(raw))){
            RespondText(400, "パスがありません");
            return false;
        }
        const auto c = FileShare::CheckPath(raw, out);
        if(c != FileShare::PathCheck::Ok){ RespondPathError(c); return false; }
        return true;
    }

    // ---- 片付け ----
    void CloseClient(){
        ShareServer& s = *server;
        s.conn.drop();
        if(s.file) s.file.close();
        s.has_client = false;
        s.phase = Phase::Head;
        s.body = Body::None;
        s.out_pos = s.out_len = 0;
        s.unread = 0;
        if(current.op != Op::None){ current.op = Op::None; revision++; }
    }
    void AbortUpload(const char* reason){
        ShareServer& s = *server;
        if(s.file) s.file.close();
        OSData::SD.remove(s.part.c_str());
        AddRecent("× %s(%s)", current.name.c_str(), reason);
        LOG_SYS_WARN("Wi-Fi転送: %s のアップロードをやめました(%s)", s.path.c_str(), reason);
        current.op = Op::None;
    }
    // 応答を送り終えた
    void FinishResponse(){
        ShareServer& s = *server;
        if(s.body == Body::File && current.op == Op::Download){
            char sz[16];
            FormatSize(current.total, sz, sizeof(sz));
            AddRecent("↓ %s(%s)", current.name.c_str(), sz);
        }
        if(s.file) s.file.close();
        s.body = Body::None;
        current.op = Op::None;
        if(s.unread > 0){
            // 読まずに閉じると相手(ブラウザ)へ応答が届かないことがあるので、本文を少し読み捨てる
            s.phase = Phase::Drain;
            s.drain_since = millis();
            return;
        }
        CloseClient();
    }

    // path の親のフォルダを頭から順に作る(?mkdirs=1。フォルダごとのアップロード)。途中にファイルがあれば false
    bool MakeParents(const char* path){
        FixedString<PICO_PATH_LEN> prefix;
        for(const char* p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')){
            prefix.assign(path, (size_t)(p - path));
            if(OSData::SD.exists(prefix.c_str())){
                if(!IsDir(prefix.c_str())) return false;
                continue;
            }
            if(!OSData::SD.mkdir(prefix.c_str())) return false;
            modified = true;
        }
        return true;
    }

    // ---- アップロード ----
    void FinishUpload(){
        ShareServer& s = *server;
        s.file.close();
        // 置き換え(SdFatの rename は行き先があると失敗する)
        if(OSData::SD.exists(s.path.c_str())) OSData::SD.remove(s.path.c_str());
        if(!OSData::SD.rename(s.part.c_str(), s.path.c_str())){
            OSData::SD.remove(s.part.c_str());
            AddRecent("× %s(置き換えられません)", current.name.c_str());
            current.op = Op::None;
            RespondText(500, "置き換えられませんでした");
            return;
        }
        modified = true;
        Changed(s.path.c_str());
        char sz[16];
        FormatSize(current.total, sz, sizeof(sz));
        AddRecent("↑ %s(%s)", current.name.c_str(), sz);
        LOG_SYS_MSG("Wi-Fi転送: %s を受け取りました(%s)", s.path.c_str(), sz);
        current.op = Op::None;
        RespondText(201, "");
    }
    // 本文を書き込む。全部届いたら置き換えて応答へ
    void ConsumeBody(const uint8_t* p, size_t n){
        ShareServer& s = *server;
        if((uint64_t)n > s.remaining) n = (size_t)s.remaining;
        if(s.unread >= n) s.unread -= n; else s.unread = 0;
        if(n > 0 && s.file.write(p, n) != n){
            AbortUpload("SDに書けません");
            RespondText(507, "SDカードに書けませんでした(空きが足りないかもしれません)");
            return;
        }
        s.remaining -= n;
        current.done += n;
        if(s.remaining == 0) FinishUpload();
    }
    void BeginUpload(const char* file_part){
        ShareServer& s = *server;
        if(!s.head.hasLength()){ RespondText(411, "Content-Length が要ります"); return; }
        const auto c = FileShare::PathFromUrl(file_part, strlen(file_part), s.path);
        if(c != FileShare::PathCheck::Ok){ RespondPathError(c); return; }
        if(strcmp(s.path.c_str(), "/") == 0 || IsDir(s.path.c_str())){ RespondText(409, "同じ名前のフォルダがあります"); return; }
        char mk[4];
        if(FileShare::QueryValue(s.head.query(), "mkdirs", mk, sizeof(mk)) && strcmp(mk, "1") == 0 &&
           !MakeParents(s.path.c_str())){
            RespondText(409, "送り先のフォルダを作れません(同じ名前のファイルがあります)");
            return;
        }
        if(!ParentIsDir(s.path.c_str())){ RespondText(409, "送り先のフォルダがありません"); return; }
        if(Busy(s.path.c_str())){ RespondText(409, "使っているファイルなので書き換えられません"); return; }
        s.part.assign(s.path);
        if(!s.part.append(".part")){ RespondText(400, "パスが長すぎます"); return; }
        s.file = OSData::SD.open(s.part.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
        if(!s.file){ RespondText(500, "SDカードに書けませんでした"); return; }
        s.remaining = s.head.contentLength();
        current.op = Op::Upload;
        current.name.assign(PICO_IO::filename(s.path.c_str()));
        current.done = 0;
        current.total = s.remaining;
        revision++;
        s.phase = Phase::Receive;
        if(s.remaining == 0) FinishUpload();
    }

    // ---- ダウンロード・一覧 ----
    void BeginDownload(const char* file_part, bool head_only){
        ShareServer& s = *server;
        FixedString<PICO_PATH_LEN> p;
        const auto c = FileShare::PathFromUrl(file_part, strlen(file_part), p);
        if(c != FileShare::PathCheck::Ok){ RespondPathError(c); return; }
        s.file = OSData::SD.open(p.c_str());
        if(!s.file){ RespondText(404, "見つかりません"); return; }
        if(s.file.isDir()){ s.file.close(); RespondText(400, "フォルダはダウンロードできません"); return; }
        const char* name = PICO_IO::filename(p.c_str());
        const uint64_t size = s.file.fileSize();
        // Range(続きからのダウンロード・音声のシーク)。1つの範囲だけ読み、ほかは全体を返す
        uint64_t first = 0, last = size ? size - 1 : 0;
        const auto range = FileShare::ParseRange(s.head.range(), size, first, last);
        FixedString<PICO_STR_1KiB> extra("Accept-Ranges: bytes\r\n");
        if(range == FileShare::RangeResult::Unsatisfiable){
            s.file.close();
            extra.appendFormat("Content-Range: bytes */%llu\r\n", (unsigned long long)size);
            BeginResponse(416, "text/plain; charset=utf-8", 0, extra.c_str());
            return;
        }
        const bool partial = (range == FileShare::RangeResult::Partial);
        if(partial){
            if(!s.file.seekSet(first)){ s.file.close(); RespondText(500, "SDカードを読めません"); return; }
            extra.appendFormat("Content-Range: bytes %llu-%llu/%llu\r\n",
                               (unsigned long long)first, (unsigned long long)last, (unsigned long long)size);
        }
        char dl[4];
        if(FileShare::QueryValue(s.head.query(), "dl", dl, sizeof(dl)) && strcmp(dl, "1") == 0){
            extra.append("Content-Disposition: attachment; filename*=UTF-8''");
            FileShare::AppendPercentEncoded(extra, name);
            extra.append("\r\n");
        }
        const uint64_t length = partial ? last - first + 1 : size;
        BeginResponse(partial ? 206 : 200, FileShare::ContentType(name), (int64_t)length, extra.c_str());
        if(head_only){ s.file.close(); return; }
        s.body = Body::File;
        s.remaining = length;
        current.op = Op::Download;
        current.name.assign(name);
        current.done = partial ? first : 0;
        current.total = size;
        revision++;
    }
    void BeginList(){
        ShareServer& s = *server;
        FixedString<PICO_PATH_LEN> p;
        char raw[PICO_PATH_LEN + 1];
        if(!FileShare::QueryValue(s.head.query(), "p", raw, sizeof(raw))) strcpy(raw, "/");
        const auto c = FileShare::CheckPath(raw, p);
        if(c != FileShare::PathCheck::Ok){ RespondPathError(c); return; }
        s.file = OSData::SD.open(p.c_str());
        if(!s.file || !s.file.isDir()){
            if(s.file) s.file.close();
            RespondText(404, "フォルダがありません");
            return;
        }
        s.listing_root = (strcmp(p.c_str(), "/") == 0);
        BeginResponse(200, "text/tab-separated-values; charset=utf-8", -1, nullptr);
        s.body = Body::Listing;
    }
    // 一覧の続きを io へ詰める(1行は名前255バイト+30バイトに収まるので、それだけ空いている間)
    void FillListing(){
        ShareServer& s = *server;
        FsFile entry;
        char name[PICO_STR_256B];
        while(s.out_len + PICO_STR_256B + 32 <= sizeof(s.io) && entry.openNext(&s.file, O_RDONLY)){
            const bool dir = entry.isDir();
            const uint64_t size = dir ? 0 : (uint64_t)entry.fileSize();
            const bool named = entry.getName(name, sizeof(name));
            entry.close();
            if(!named) continue;
            if(s.listing_root){
                char p[PICO_STR_256B + 2];
                snprintf(p, sizeof(p), "/%s", name);
                if(FileShare::IsSystemPath(p)) continue;
            }
            const int n = snprintf((char*)s.io + s.out_len, sizeof(s.io) - s.out_len, "%s\t%c\t%llu\n",
                                   name, dir ? 'd' : 'f', (unsigned long long)size);
            if(n > 0) s.out_len += (size_t)n;
        }
    }

    // ---- 書き換え ----
    void DoMkdir(){
        FixedString<PICO_PATH_LEN> p;
        if(!QueryPath("p", p)) return;
        if(strcmp(p.c_str(), "/") == 0 || OSData::SD.exists(p.c_str())){ RespondText(409, "同じ名前があります"); return; }
        if(!ParentIsDir(p.c_str())){ RespondText(409, "親のフォルダがありません"); return; }
        if(!OSData::SD.mkdir(p.c_str())){ RespondText(500, "作れませんでした"); return; }
        modified = true;
        AddRecent("+ %s", PICO_IO::filename(p.c_str()));
        RespondText(201, "");
    }
    void DoRemove(){
        FixedString<PICO_PATH_LEN> p;
        if(!QueryPath("p", p)) return;
        if(strcmp(p.c_str(), "/") == 0){ RespondText(403, "SDの全体は消せません"); return; }
        if(!OSData::SD.exists(p.c_str())){ RespondText(404, "見つかりません"); return; }
        if(Busy(p.c_str())){ RespondText(409, "使っているので消せません"); return; }
        if(!PICO_IO::removeRecursive(p.c_str())){ RespondText(500, "消せませんでした"); return; }
        modified = true;
        Changed(p.c_str());
        AddRecent("- %s", PICO_IO::filename(p.c_str()));
        LOG_SYS_MSG("Wi-Fi転送: %s を消しました", p.c_str());
        RespondText(204, "");
    }
    void DoMove(){
        FixedString<PICO_PATH_LEN> from, to;
        if(!QueryPath("p", from) || !QueryPath("to", to)) return;
        if(strcmp(from.c_str(), "/") == 0 || strcmp(to.c_str(), "/") == 0){ RespondText(403, "SDの全体は動かせません"); return; }
        if(!OSData::SD.exists(from.c_str())){ RespondText(404, "見つかりません"); return; }
        if(OSData::SD.exists(to.c_str())){ RespondText(409, "同じ名前があります"); return; }
        if(PICO_IO::isSameOrInside(to.c_str(), from.c_str())){ RespondText(409, "フォルダを自分の中へは移せません"); return; }
        if(!ParentIsDir(to.c_str())){ RespondText(409, "移す先のフォルダがありません"); return; }
        if(Busy(from.c_str()) || Busy(to.c_str())){ RespondText(409, "使っているので動かせません"); return; }
        if(!OSData::SD.rename(from.c_str(), to.c_str())){ RespondText(500, "名前を変えられませんでした"); return; }
        modified = true;
        Changed(from.c_str());
        Changed(to.c_str());
        AddRecent("→ %s", PICO_IO::filename(to.c_str()));
        RespondText(204, "");
    }

    // ---- 見出しが揃ったら ----
    void Dispatch(){
        ShareServer& s = *server;
        const auto st = s.head.state();
        s.unread = s.head.hasLength() ? s.head.contentLength() : 0;
        if(st == FileShare::RequestHead::State::TooLarge){ RespondText(431, "要求が大きすぎます"); return; }
        if(st != FileShare::RequestHead::State::Done){ RespondText(400, "要求を読めません"); return; }

        char ip[20] = "";
#if defined(PICOOS_PC)
        strcpy(ip, "127.0.0.1");
#else
        if(direct) NetworkFunctions::SetupApAddress(ip, sizeof(ip));
        else if(NetworkFunctions::IsConnected()) NetworkFunctions::CurrentAddress(ip, sizeof(ip));
#endif
        if(!FileShare::HostAllowed(s.head.host(), NetworkFunctions::hostname.c_str(), ip, ListenPort())){
            RespondText(403, "この名前では開けません(本体の名前かIPアドレスで開いてください)");
            return;
        }
        if(LockFunctions::IsLocked()){ RespondText(423, "本体の画面ロックを解いてください"); return; }

        const char* file_part = nullptr;
        const Route route = FileShare::Classify(s.head.method(), s.head.path(), &file_part);
        const bool head_only = (s.head.method() == FileShare::Method::Head);
        if(route == Route::NotFound){ RespondText(404, "見つかりません"); return; }
        if(route == Route::MethodNotAllowed){ RespondText(405, "その方法では使えません"); return; }
        if(route == Route::Page){
            BeginResponse(200, "text/html; charset=utf-8", (int64_t)FileShare::PageHtmlLength(), nullptr);
            if(!head_only){
                s.body = Body::Memory;
                s.mem = FileShare::kPageHtml;
                s.mem_len = FileShare::PageHtmlLength();
                s.mem_pos = 0;
            }
            return;
        }

        // ここから先はキーが要る
        const uint32_t now = millis();
        if(throttle.blocked(now)){ RespondText(429, "キーを何度も間違えたので、しばらく待ってください"); return; }
        char cookie_key[FileShare::kKeyLen + 8] = "";
        const char* given = s.head.key();
        if(!given[0] && FileShare::CookieValue(s.head.cookie(), "pk", cookie_key, sizeof(cookie_key))) given = cookie_key;
        if(!FileShare::KeyEquals(given, share_key)){
            throttle.fail(now);
            RespondText(401, "キーが違います");
            return;
        }
        if(FileShare::IsMutating(route) && !FileShare::KeyEquals(s.head.key(), share_key)){
            RespondText(403, "書き換えには X-Pico-Key の見出しが要ります");
            return;
        }
        throttle.success();
        if(!OSData::SD_usable){ RespondText(503, "SDカードが使えません"); return; }

        switch(route){
            case Route::Info: {
                FixedString<PICO_STR_256B> json("{\"name\":\"");
                FileShare::AppendJsonEscaped(json, NetworkFunctions::hostname.c_str());
                json.append("\"}");
                Respond(200, "application/json", json.c_str());
                break;
            }
            case Route::List:     BeginList(); break;
            case Route::Download: BeginDownload(file_part, head_only); break;
            case Route::Upload:   BeginUpload(file_part); break;
            case Route::Mkdir:    DoMkdir(); break;
            case Route::Remove:   DoRemove(); break;
            case Route::Move:     DoMove(); break;
            default:              RespondText(404, "見つかりません"); break;
        }
    }

    // 送れる分を送り、空いたら本文の続きを詰める。進んだら true
    bool Pump(){
        ShareServer& s = *server;
        if(s.out_pos < s.out_len){
            const int w = s.conn.write(s.io + s.out_pos, s.out_len - s.out_pos);
            if(w < 0){
                if(current.op == Op::Download) AddRecent("× %s(切れました)", current.name.c_str());
                CloseClient();
                return false;
            }
            if(w == 0) return false;
            s.out_pos += (size_t)w;
            s.last_progress = millis();
            return true;
        }
        s.out_pos = s.out_len = 0;
        switch(s.body){
            case Body::Memory: {
                const size_t n = (s.mem_len - s.mem_pos < sizeof(s.io)) ? s.mem_len - s.mem_pos : sizeof(s.io);
                memcpy(s.io, s.mem + s.mem_pos, n);
                s.mem_pos += n;
                s.out_len = n;
                break;
            }
            case Body::File: {
                if(s.remaining == 0) break;
                const size_t want = (s.remaining < sizeof(s.io)) ? (size_t)s.remaining : sizeof(s.io);
                const int r = s.file.read(s.io, want);
                if(r <= 0){
                    AddRecent("× %s(SDを読めません)", current.name.c_str());
                    CloseClient();
                    return false;
                }
                s.remaining -= (uint64_t)r;
                current.done += (uint64_t)r;
                s.out_len = (size_t)r;
                break;
            }
            case Body::Listing: FillListing(); break;
            case Body::None: break;
        }
        if(s.out_len == 0){ FinishResponse(); }
        return true;
    }

    // 1歩進める。進んだら true(時間の残る限り続けて呼ぶ)
    bool Step(){
        ShareServer& s = *server;
        const uint32_t now = millis();
        if(!s.has_client){
            if(!s.conn.accept()) return false;
            s.has_client = true;
            s.last_progress = now;
            s.head.reset();
            s.phase = Phase::Head;
            return true;
        }
        switch(s.phase){
            case Phase::Head: {
                uint8_t buf[512];
                const int r = s.conn.read(buf, sizeof(buf));
                if(r < 0){ CloseClient(); return false; }
                if(r == 0) return false;
                s.last_progress = now;
                size_t used = 0;
                if(s.head.feed(buf, (size_t)r, used) == FileShare::RequestHead::State::Reading) return true;
                Dispatch();
                const size_t rest = (size_t)r - used;
                if(s.phase == Phase::Receive) ConsumeBody(buf + used, rest);
                else s.unread = (s.unread > rest) ? s.unread - rest : 0;
                return true;
            }
            case Phase::Receive: {
                const size_t want = (s.remaining < sizeof(s.io)) ? (size_t)s.remaining : sizeof(s.io);
                const int r = s.conn.read(s.io, want);
                if(r < 0){ AbortUpload("切れました"); CloseClient(); return false; }
                if(r == 0) return false;
                s.last_progress = now;
                ConsumeBody(s.io, (size_t)r);
                return true;
            }
            case Phase::Send:
                return Pump();
            case Phase::Drain: {
                const int r = s.conn.read(s.io, sizeof(s.io));
                if(r < 0 || now - s.drain_since >= kDrainMs){ CloseClient(); return false; }
                if(r == 0) return false;
                s.unread = ((uint64_t)r >= s.unread) ? 0 : s.unread - (uint64_t)r;
                if(s.unread == 0) CloseClient();
                return true;
            }
        }
        return false;
    }
}

bool FileShareFunctions::Start(FixedString<PICO_STR_LL>& err){
    if(state != State::Idle) return true;
    if(!OSData::SD_usable){ err.assign("SDカードが使えません"); return false; }
    uint8_t rnd[FileShare::kKeyLen];
    SecretAead::Random(rnd, sizeof(rnd));
    FileShare::MakeKey(rnd, share_key);
#if defined(PICOOS_PC)
    // PCビルドの自動の確かめ用(curlから叩く)。実機には無い
    if(const char* k = getenv("PICOOS_SHARE_KEY")){
        if(strlen(k) == FileShare::kKeyLen) strcpy(share_key, k);
    }
#endif
    throttle = FileShare::Throttle();
    current = Activity();
    recent_count = 0;
    modified = false;

    server = new ShareServer();
    if(!server->conn.begin(err)){
        delete server;
        server = nullptr;
        return false;
    }
    state = State::Running;
    revision++;
    FixedString<PICO_STR_L> by_name, by_ip;
    PageUrls(by_name, by_ip);
    LOG_SYS_MSG("Wi-Fi転送: %s で待っています", by_ip.empty() ? by_name.c_str() : by_ip.c_str());
    return true;
}

void FileShareFunctions::Update(){
    if(state != State::Running || !server) return;
    ShareServer& s = *server;
    if(LockFunctions::IsLocked()){
        // ロックの間は受けない(途中の転送もやめる)
        if(s.has_client){
            if(s.phase == Phase::Receive) AbortUpload("画面ロック");
            CloseClient();
        }
        return;
    }
    const uint32_t start = millis();
    while(Step()){
        if(millis() - start >= kBudgetMs) break;
    }
    if(s.has_client && millis() - s.last_progress >= kIdleTimeoutMs && s.phase != Phase::Drain){
        if(s.phase == Phase::Receive) AbortUpload("時間切れ");
        else if(current.op == Op::Download) AddRecent("× %s(時間切れ)", current.name.c_str());
        CloseClient();
    }
}

void FileShareFunctions::Stop(){
    if(state == State::Idle) return;
    if(server){
        if(server->has_client && server->phase == Phase::Receive) AbortUpload("やめました");
        CloseClient();
        server->conn.end();
        delete server;
        server = nullptr;
    }
    if(direct){
        NetworkFunctions::StopSetupAp(); // 保存済みのWi-Fiへの自動接続へ戻る
        direct = false;
    }
    state = State::Idle;
    background = false;
    share_key[0] = '\0';
    revision++;
    LOG_SYS_MSG("Wi-Fi転送: 待ち受けをやめました");
    if(modified && sd_changed) sd_changed();
    modified = false;
}

void FileShareFunctions::SetBackground(bool on){
    background = on && state == State::Running;
}

bool FileShareFunctions::InBackground(){ return background; }

bool FileShareFunctions::SetDirect(bool on, FixedString<PICO_STR_LL>& err){
    if(state != State::Running || !server) return false;
    if(on == direct) return true;
    // 途中の転送はやめ、待ち受けも作り直す(Wi-Fiの役割を替えると、今の接続は使えなくなる)
    if(server->has_client){
        if(server->phase == Phase::Receive) AbortUpload("Wi-Fiを切り替えました");
        CloseClient();
    }
    server->conn.end();
    if(on){
        snprintf(ap_ssid, sizeof(ap_ssid), "%.26s-share", NetworkFunctions::hostname.c_str());
        uint8_t rnd[8];
        SecretAead::Random(rnd, sizeof(rnd));
        for(int i = 0; i < 8; i++) ap_password[i] = (char)('0' + rnd[i] % 10);
        ap_password[8] = '\0';
        if(!NetworkFunctions::StartSetupAp(ap_ssid, ap_password)){
            err.assign("アクセスポイントを始められませんでした");
            server->conn.begin(err); // 元のWi-Fiのまま続ける
            return false;
        }
        direct = true;
        LOG_SYS_MSG("Wi-Fi転送: アクセスポイント %s で待っています", ap_ssid);
    }else{
        NetworkFunctions::StopSetupAp();
        direct = false;
        LOG_SYS_MSG("Wi-Fi転送: 保存済みのWi-Fiへ戻ります");
    }
    revision++;
    if(!server->conn.begin(err)){
        LOG_SYS_WARN("Wi-Fi転送: 待ち受けを始め直せませんでした(%s)", err.c_str());
        return false;
    }
    return true;
}

bool FileShareFunctions::IsDirect(){ return direct; }
const char* FileShareFunctions::ApSsid(){ return ap_ssid; }
const char* FileShareFunctions::ApPassword(){ return ap_password; }
int FileShareFunctions::ApClients(){ return direct ? NetworkFunctions::SetupApClients() : 0; }

FileShareFunctions::State FileShareFunctions::GetState(){ return state; }

void FileShareFunctions::PageUrls(FixedString<PICO_STR_L>& by_name, FixedString<PICO_STR_L>& by_ip){
    by_name.clear();
    by_ip.clear();
#if defined(PICOOS_PC)
    char url[48];
    snprintf(url, sizeof(url), "http://127.0.0.1:%u/", (unsigned)ListenPort());
    by_ip.assign(url);
#else
    if(direct){
        // 本体のAPには名前を引く仕組み(DNS・mDNS)が無いのでIPだけ
        char ip[20] = "";
        NetworkFunctions::SetupApAddress(ip, sizeof(ip));
        if(ip[0]) by_ip.appendFormat("http://%s/", ip);
        return;
    }
    by_name.appendFormat("http://%s.local/", NetworkFunctions::hostname.c_str());
    if(NetworkFunctions::IsConnected()){
        char ip[20] = "";
        NetworkFunctions::CurrentAddress(ip, sizeof(ip));
        if(ip[0]) by_ip.appendFormat("http://%s/", ip);
    }
#endif
}

void FileShareFunctions::KeyUrl(FixedString<PICO_STR_LL>& out){
    FixedString<PICO_STR_L> by_name, by_ip;
    PageUrls(by_name, by_ip);
    out.assign(by_ip.empty() ? by_name : by_ip);
    out.append("?k=");
    out.append(share_key);
}

const char* FileShareFunctions::Key(){ return share_key; }
const FileShareFunctions::Activity& FileShareFunctions::Current(){ return current; }
int FileShareFunctions::RecentCount(){ return recent_count; }
const char* FileShareFunctions::Recent(int i){ return (i >= 0 && i < recent_count) ? recent[i].c_str() : ""; }
uint32_t FileShareFunctions::Revision(){ return revision; }
