#pragma once

// 「ユーザーへ見せるべき失敗」をログと画面の両方へ出すための共通口
// (CLAUDE.md「Lua着手前の受け皿の状態」の「エラーの見せ方」に対応)。
//
// 従来LOG_SYS_FAIL/LOG_APP_FAILはSDのログにしか残らず、画面上には何も出なかった。
// 将来Luaスクリプトのエラーをpcallで拾った後に「アプリが落ちました」をユーザーへ
// 見せる先が無いという穴を塞ぐため、単発のMsgDialogを自分で生成・登録・破棄する
// ところまでを1関数にまとめてある(呼び出し元がMsgDialogの生成/登録/破棄の作法を
// 毎回書かずに済む)。
namespace ErrorFunctions {
    // messageをログ(LOG_APP_FAIL)へ出し、画面にもMsgDialogで表示する。
    // 長い文章はダイアログの表示上折り返されるだけで切り詰まりはしない
    // (Label::setMaxWidthに任せているため)。
    // 呼び出しは何度でも安全(ダイアログはこの呼び出しごとに新規生成し、閉じられたら自己破棄する)。
    void ShowFatal(const char* message);
}
