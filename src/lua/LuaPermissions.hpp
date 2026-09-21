#pragma once

// Luaアプリへ許す操作の粗い(all-or-nothingの2値)許可フラグ。
// 既定はどちらもfalse(最小権限)。アプリを登録する側(App_List.cpp等、将来はSD走査で
// 見つけたアプリを登録する側)が、そのアプリに必要な分だけ明示的に立てる。
//
// LuaScene(1つのLuaアプリ=1つのLuaEngine)の構築時に1回だけ決まり、実行中に
// スクリプト側から変更する手段は無い(pico.*には対応するsetterを生やしていない)。
//
//   network            … pico.http_request/pico.http_cancelを使えるか
//   sd_outside_app_dir … pico.sd_*/pico.image_loadで、スクリプト自身のディレクトリ
//                         (LuaEngineのapp_dir。通常はスクリプトの親ディレクトリ)の
//                         外を読み書きできるか。falseの間はapp_dir配下だけに閉じる
//
// 経路ごとの細かい許可(パスのホワイトリスト、ホスト単位のネットワーク制限等)は
// 今のところ無い。「そのアプリの持ち場の外へ出られるか出られないか」の二値だけを見る、
// 最初の一歩としての粗い実装(CLAUDE.md「Luaアプリの権限管理」参照)。
struct LuaPermissions {
    bool network = false;
    bool sd_outside_app_dir = false;
};
