#pragma once

// SD上の `/lua/apps/` を走査し、見つけたLuaアプリをランチャの登録簿
// (AppFunctions)へ自動登録する。「Luaアプリを増やすたびApp_List.cppへ手で
// 1行足す」だった従来の唯一の経路に加え、SDへスクリプトを置くだけでランチャに
// タイルが出る経路を用意する(「Lua版アプリストア」の土台)。
//
// レイアウト: `/lua/apps/<名前>/main.lua` という「サブディレクトリ1つ=アプリ1つ」
// の構成にしてある。理由は2つ:
//   - AppEntry::nameにそのまま使える表示名を、ファイル名の拡張子を弄るような
//     加工無しで得られる(SDのファイル名がそのままタイル名になる)
//   - LuaScene::onEnter()はスクリプト自身の親ディレクトリをapp_dir(SDアクセスの
//     閉じ込め先)として使う。サブディレクトリを分けておけば、権限が既定
//     (sd_outside_app_dir=false)のままでも、アプリごとに別のapp_dirを持て、
//     見つかった複数のアプリが互いのファイルを読み書きできてしまう事故を防げる
//     (仮に全部を/lua/apps/直下へフラットに置くと、app_dirが全アプリ共通の
//     /lua/apps/になってしまい、この分離ができない)
//
// **権限は既定値(LuaPermissions{}、network/sd_outside_app_dirとも false)固定。**
// SDに置かれているだけで中身を検証していないスクリプトへ、走査した側が
// 勝手に強い権限を与えないための判断(CLAUDE.md「権限」参照)。ネットワークや
// app_dir外のSDアクセスがどうしても要るLuaアプリは、従来通りApp_List.cppへ
// 専用の生成関数(MakeLuaHelloScene()と同じ形)を書いて手動登録すること。
namespace LuaAppScanner {
    // `/lua/apps/` 直下の各サブディレクトリについて、`main.lua` があれば
    // `AppFunctions::Register()` でランチャへ登録する。
    // SD無し/ディレクトリが存在しない場合は何もせず0を返す(SD_Functions同様、
    // 「無くても起動する」を維持する)。
    // 戻り値は実際に登録できた件数(AppFunctions::kMaxAppsの上限に当たった分は
    // Register()側の警告ログに任せ、ここではカウントしない)。
    int Scan();
}
