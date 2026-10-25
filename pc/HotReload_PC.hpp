// Luaアプリのホットリロード(PCビルドのネイティブ専用。picoos_pc --app NAME --hot-reload)。
//
// --app で開いたLuaアプリのフォルダ(main.lua のあるディレクトリ)を見張り、中の .lua と app.cfg が
// 保存されたら、同じ引数で picoos_pc を起動し直す(電源メニューの「再起動」と同じ道)。起動し直すと
// --app がまたそのアプリを開くので、「保存 → 数秒で新しいコードの画面」になる。
//
// その場でLuaの状態だけ作り直す方式にしないのは、画面の積み重ね・タイマー・HTTP・画像などアプリが
// 持っていたものを確実に片付けられるのが「プロセスごと」だけだから(PCの起動は1秒かからない)。
// SDへ書いたもの(store.json 等)は残るので、保存データの続きから試せる。
//
// 見るのは名前と更新時刻と大きさだけ。.lua と app.cfg に限るのは、アプリ自身が書くファイル
// (store.json・settings.cfg・ダウンロード)で起動し直し続けないため。エディタが「一時ファイルへ書いて
// 置き換える」のに途中で拾わないよう、変わった後もう一度同じ中身に見えてから起動し直す。
#pragma once

#include <dirent.h>
#include <sys/stat.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <SdFat.h>                    // PicoOsSdHost::toHostPath(SDのパス → PCのパス)
#include "services/App_Functions.hpp"

namespace PicoOsHotReload {
    inline bool        enabled = false;  // --hot-reload
    inline std::string dir;              // 見張るPCのディレクトリ(空なら見張らない)
    inline uint64_t    signature = 0;    // 起動したときの中身
    inline uint64_t    seen = 0;         // 前回見た中身(変わった後、2回続けて同じなら起動し直す)
    inline std::chrono::steady_clock::time_point next_poll;

    constexpr int kPollMs = 250;
    constexpr int kMaxDepth = 4;  // ui/menu.lua のような下のフォルダも見る(深すぎるものは見ない)

    inline bool Watched(const char* name)
    {
        const size_t n = strlen(name);
        return (n > 4 && strcmp(name + n - 4, ".lua") == 0) || strcmp(name, "app.cfg") == 0;
    }

    // FNV-1a で 名前・更新時刻・大きさ を混ぜる。readdir の順は決まっていないので、ファイルごとの
    // 値を足し合わせる(順番に依らない)
    inline uint64_t Mix(uint64_t h, const void* p, size_t n)
    {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
        return h;
    }

    inline uint64_t Scan(const std::string& path, int depth)
    {
        DIR* d = opendir(path.c_str());
        if (!d) return 0;
        uint64_t sum = 0;
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] == '.') continue;
            const std::string child = path + "/" + e->d_name;
            struct stat st;
            if (stat(child.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (depth < kMaxDepth) sum += Mix(Scan(child, depth + 1), e->d_name, strlen(e->d_name));
                continue;
            }
            if (!Watched(e->d_name)) continue;
            uint64_t h = 1469598103934665603ull;
            h = Mix(h, e->d_name, strlen(e->d_name));
            h = Mix(h, &st.st_mtim.tv_sec, sizeof(st.st_mtim.tv_sec));
            h = Mix(h, &st.st_mtim.tv_nsec, sizeof(st.st_mtim.tv_nsec));
            h = Mix(h, &st.st_size, sizeof(st.st_size));
            sum += h;
        }
        closedir(d);
        return sum;
    }

    // setup() と起動時のアプリを開いた後に呼ぶ。appName(--app)のLuaアプリのフォルダを見張り始める
    inline void Start(const char* appName)
    {
        if (!enabled) return;
        if (!appName || !appName[0]) {
            printf("[PC] --hot-reload は --app でLuaアプリを開いたときだけ効きます\n");
            return;
        }
        for (int i = 0; i < AppFunctions::Count(); i++) {
            const AppEntry* entry = AppFunctions::Get(i);
            if (!entry || !(entry->name == appName)) continue;
            const char* arg = entry->arg.c_str();
            const size_t n = strlen(arg);
            const char* slash = strrchr(arg, '/');
            if (n < 4 || strcmp(arg + n - 4, ".lua") != 0 || !slash) break;
            dir = PicoOsSdHost::toHostPath(std::string(arg, slash - arg).c_str());
            signature = seen = Scan(dir, 0);
            next_poll = std::chrono::steady_clock::now() + std::chrono::milliseconds(kPollMs);
            printf("[PC] ホットリロード: %s を見張ります(.lua / app.cfg を保存すると起動し直します)\n", dir.c_str());
            return;
        }
        printf("[PC] --hot-reload: %s はLuaアプリではないので見張りません\n", appName);
    }

    // 毎フレーム呼ぶ。起動し直すべきときtrue
    inline bool Poll()
    {
        if (dir.empty()) return false;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_poll) return false;
        next_poll = now + std::chrono::milliseconds(kPollMs);
        const uint64_t s = Scan(dir, 0);
        if (s == signature) { seen = s; return false; }
        if (s != seen) { seen = s; return false; }  // まだ書いている途中かもしれない。次も同じなら
        printf("[PC] ホットリロード: 変更を見つけたので起動し直します\n");
        return true;
    }
}
