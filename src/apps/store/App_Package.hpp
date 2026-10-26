#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

// SDの直下に置いたアプリのパッケージ(.papp)を入れる係(形式は docs/APP_REPO.md「パッケージ(.papp)」)。
// Webのアプリストアのページ(https://kimu1109.github.io/pico-os-apps/)から落としたファイルを
// そのままSDの直下へ置けば、次の起動(とUSBドライブを終えたとき)に /lua/apps/<dir>/ へ入る。
//
// 確かめることはストアから入れるときと同じ(作者の署名・files.tsv と各ファイルのSHA-256・app.cfg の id/版/権限・
// 覚えた作者の鍵・古い版を断る)。カタログの署名だけは無い(SDへ置ける人はアプリを直接置けるので、守りは弱くならない)。
// 入れたパッケージは消し、入れられなかったものは <名前>.err へ改名する(起動のたびに同じ失敗を繰り返さない)。
namespace AppPackage {
    constexpr const char* kExtension = ".papp";
    constexpr const char* kMagic = "pico-os-app";
    constexpr int kFormat = 1;
    // 1回で入れる数(残りは次の起動で)
    constexpr int kMaxPerRun = 8;

    // path のパッケージを入れる。結果の1行(日本語)を message へ。入れたアプリの表示名を name へ(任意)
    bool Install(const char* path, FixedString<PICO_STR_LL>& message, FixedString<PICO_STR_M>* name = nullptr);

    // SDの直下の *.papp を全部入れる(入れた数を返す)。結果は PostPendingNotice() で通知する
    int InstallPending();
    // InstallPending() の結果を通知に出して忘れる(何も無ければ何もしない)。通知の Setup() より後で呼ぶ
    void PostPendingNotice();
}
