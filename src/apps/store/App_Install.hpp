#pragma once

#include <cstddef>
#include <cstdint>

#include "apps/store/App_Repo.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"

// 取ってきたアプリを /lua/apps/<dir>/ へ入れる係(docs/APP_REPO.md「本体が確かめること」)。
// 通信で取る AppStore と、SDに置いたパッケージ(.papp)から入れる AppPackage が同じ手順を使う:
//   /sys/store/staging/files.tsv と /sys/store/staging/app/ へ全部置く → CheckFilesList() →
//   (各ファイルのSHA-256を確かめる) → CheckStagedConfig() → Commit()
// どの関数も失敗したら利用者へ見せる1行(日本語)を返し、成功なら nullptr を返す。
namespace AppInstall {
    // 入っているかどうか(AppStore::Installed と同じもの)
    enum class Installed : uint8_t {
        No,       // 入っていない
        Same,     // 同じ版が入っている
        Update,   // 古い版が入っている
        Newer,    // 一覧(パッケージ)より新しい版が入っている(手元で書き換えた等)
        Foreign,  // 同じ名前のディレクトリに、ストアのものでない(idが無い)アプリがある
        Conflict, // 同じ名前のディレクトリに、別のidのアプリがある(入れられない)
        KeyChanged, // 入れたときと作者の鍵が違う(更新できない。削除すれば新しい鍵で入れられる)
    };

    // files.tsv を1行ずつ読む作業場所(スタックに置かないため、呼び出し側のメンバか new した所に置く)
    struct Work {
        char line[AppRepo::kMaxLineBytes + 2];
        char aux[AppRepo::kMaxLineBytes + 2];
        AppRepo::FileEntry file;
    };

    // 本体の通し番号(既定は FwVersion::Serial())。ホストテストの差し替え口
    inline uint32_t (*os_serial_hook)() = nullptr;
    uint32_t OsSerial();
    // e の min_os を今の本体が満たさなければ、利用者へ見せる1行を返す(満たせば nullptr)
    const char* CheckOs(const AppRepo::Entry& e);

    // インストール先(/lua/apps/<dir>)
    bool AppDir(const AppRepo::Entry& e, FixedString<PICO_PATH_LEN>& out);
    // 入っているかどうか。version には入っている版(入っていなければ空)、perms には入っているものの権限
    Installed Check(const AppRepo::Entry& e, FixedString<PICO_STR_S>* version = nullptr, uint8_t* perms = nullptr);

    // 覚えている作者の鍵(/sys/store/installed/<id>.key)。無ければfalse
    bool ReadPinnedKey(const char* id, uint8_t out[32]);
    bool WritePinnedKey(const char* id, const uint8_t key[32]);
    // 入れたときのファイルの一覧・作者の鍵・前の版・見送りの印を忘れる(削除したとき)
    void Forget(const char* id);

    // ---- 前の版(docs/APP_REPO.md「前の版へ戻す」) ----
    // ストアで入れたアプリを更新すると、前の版のファイル(前の版の一覧に載っていたもの)を
    // /sys/store/prev/<id>/app/ へ移して残す(1つ前の版だけ)。Rollback() で戻せる。
    // 前の版があればその版を version へ
    bool PreviousVersion(const char* id, FixedString<PICO_STR_S>& version);
    // 前の版へ戻す: 今の版のファイル(今の一覧に載っているもの)を消し、前の版のファイルを戻す。
    // 戻した版を「見送り」(/sys/store/installed/<id>.hold)として覚え、まとめて更新・更新の知らせから外す。
    // dir … インストール先(/lua/apps/<dir>)。戻した版を restored へ(任意)
    const char* Rollback(const char* id, const char* dir, Work& w, FixedString<PICO_STR_S>* restored = nullptr);
    // 見送っている版(前の版へ戻したときの新しいほうの版)。無ければfalse
    bool HeldVersion(const char* id, FixedString<PICO_STR_S>& version);

    // files.tsv(path)の n 番目(0始まり。空行・'#'の行は数えない)の行を w.file へ
    bool ReadFileLine(const char* path, int n, Work& w);

    // 置き場の files.tsv を全部確かめる: 形式・パスの並び(大小を区別せず昇順=重なり無し)・数・合計・必須のファイル。
    // 通ればファイルの数を count へ
    const char* CheckFilesList(const AppRepo::Entry& e, Work& w, int& count);
    // 置き場の app.cfg の id・版が e と同じで、権限が e.perms を超えないか
    const char* CheckStagedConfig(const AppRepo::Entry& e);
    // 置き場から /lua/apps/<dir>/ へ1つずつ移し、前の版にだけあったファイルを消し、作者の鍵を覚え、
    // ランチャの登録簿へ入れる。allow_replace … 同じ名前の、idの無いアプリへ上書きしてよいか。
    // 成功したら置き場を片付ける(失敗したときは呼び出し側が消す)
    const char* Commit(const AppRepo::Entry& e, bool allow_replace, Work& w);

    // path の親ディレクトリを作る(実機のSdFat::mkdirは親も作る)
    bool MakeParent(const char* path);
    // dir + "/" + rel
    bool JoinRel(FixedString<PICO_PATH_LEN>& out, const char* dir, const char* rel);
}
