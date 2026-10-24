#pragma once

#include "util/FixedString.hpp"
#include "consts.hpp"

// 最近開いたファイル(/sys/recent_files.txt)。テキストエディタで開いた/保存したファイルと、
// ファイルビューワーで開いたファイルを覚える。ファイルアプリの「他」→「最近開いたファイル」・
// エディタのメニュー・メモアプリの並び順で使う。
//
// 1行1パスで新しい順。Touch() のたびに、そのパスを先頭へ置いて残りを写し直す(kMaxFiles 件まで。古いものから忘れる)。
// 一覧はメモリに持たず、要るときに読む側の配列へ読む。
namespace RecentFiles {
    constexpr int kMaxFiles = 20;

    // path を一番新しいものとして覚える
    bool Touch(const char* path);
    // 新しい順に最大 max 件。読めなければ 0
    int List(FixedString<PICO_PATH_LEN>* out, int max);
    // 一覧から外す(消した・見つからないファイル)
    bool Forget(const char* path);
    // 名前を変えた・移したファイル(とフォルダの中)を、新しいパスへ書き換える
    bool Rename(const char* old_path, const char* new_path);
}
