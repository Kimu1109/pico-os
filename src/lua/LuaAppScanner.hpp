#pragma once

// SD上の `/lua/apps/` を走査し、見つけたLuaアプリをランチャの登録簿
// (AppFunctions)へ自動登録する。「Luaアプリを増やすたびApp_List.cppへ手で
// 1行足す」だった従来の唯一の経路に加え、SDへスクリプトを置くだけでランチャに
// タイルが出る経路を用意する(「Lua版アプリストア」の土台)。
//
// レイアウト: `/lua/apps/<名前>/main.lua` という「サブディレクトリ1つ=アプリ1つ」
// の構成にしてある。理由は2つ:
//   - AppEntry::nameにそのまま使える表示名を、ファイル名の拡張子を弄るような
//     加工無しで得られる(SDのファイル名がそのままタイル名になる。ただし下記
//     app.cfgのnameが優先される)
//   - LuaScene::onEnter()はスクリプト自身の親ディレクトリをapp_dir(SDアクセスの
//     閉じ込め先)として使う。サブディレクトリを分けておけば、権限が既定
//     (sd_outside_app_dir=false)のままでも、アプリごとに別のapp_dirを持て、
//     見つかった複数のアプリが互いのファイルを読み書きできてしまう事故を防げる
//     (仮に全部を/lua/apps/直下へフラットに置くと、app_dirが全アプリ共通の
//     /lua/apps/になってしまい、この分離ができない)
//
// 【設定ファイル(app.cfg、任意)】 `/lua/apps/<名前>/app.cfg` があれば
// PICO_Config(key=value形式)として読み、以下のキーを認識する(全て省略可):
//   name                          … 表示名。指定があればディレクトリ名より優先する
//   description / version         … 説明文・バージョン表記。現状はスキャン時に
//                                    ログへ出すだけで、AppEntryへは保持しない
//                                    (表示先のUIがまだ無いため。将来アプリ情報画面を
//                                    作る際は、app_dirから再度app.cfgを読み直す形でよい)
//   icon                          … アプリディレクトリ内の相対パス(.pimg形式)。
//                                    指定があればランチャのタイルアイコンとして使う
//                                    (無指定/読み込み失敗時はIconID::AppBoxへ落ちる)
//   permission_network            … pico.http_request/http_cancelを許すか(true/false)
//   permission_sd_outside_app_dir … pico.sd_*/image_loadでapp_dirの外を触れるか
// **どちらの権限も未指定なら既定でfalse(最小権限)。** SDに置かれているだけで
// 中身を検証していないスクリプトへ、勝手に強い権限を与えないための判断
// (CLAUDE.md「権限」参照)。app.cfg自体が無いアプリも同様に両方falseで登録される。
namespace LuaAppScanner {
    // `/lua/apps/` 直下の各サブディレクトリについて、`main.lua` があれば
    // `AppFunctions::Register()` でランチャへ登録する。
    // SD無し/ディレクトリが存在しない場合は何もせず0を返す(SD_Functions同様、
    // 「無くても起動する」を維持する)。
    // 戻り値は実際に登録できた件数(AppFunctions::kMaxAppsの上限に当たった分は
    // Register()側の警告ログに任せ、ここではカウントしない)。
    int Scan();
}
