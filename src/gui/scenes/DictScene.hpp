#pragma once

#include "gui/scenes/Scene.hpp"
#include "gui/widgets/Button.hpp"
#include "gui/widgets/Textbox.hpp"
#include "gui/widgets/Label.hpp"
#include "gui/widgets/ScrollList.hpp"
#include "gui/widgets/ScrollContainer.hpp"
#include "dict/Word_Dict.hpp"

// 標準アプリの辞書(英和/和英)。
//
// 検索ロジック本体はWordDictionary(src/dict/)が持つ。このシーンは
// 「入力欄+検索ボタン+状態表示+結果一覧+詳細表示」を並べるだけの薄い皮
// (ClocksScene/FileExplorerSceneと同じ形)。
//
// **前方一致はsearch()が同期的に即返すが、語の途中の一致(部分一致)は
// State::Scanning/ScanningSuffixとしてupdate()を毎フレーム呼んで少しずつ
// 拾う**ため、onUpdate()でdict_.update()を回し、新しく見つかった分だけ
// 一覧へ追記する(全部揃うまで画面が止まらない)。部分一致は通常
// サフィックスインデックス(dict_suffixes.tsv、script/build_dict_suffix_index.py
// が生成)経由のScanningSuffixで高速に拾えるが、インデックスが無い/壊れて
// いる場合のみ旧来のScanning(ファイル全体走査)へ自動フォールバックする
// (詳細はWord_Dict.hppの設計コメント参照)。
//
// MarkdownSceneと同じく、このシーンのオブジェクトは「数十バイト」の
// 他シーンと違いWordDictionary(実測約53KB。サフィックス索引の分だけ
// 従来の~43KBより増えている)を持つため大きい。辞書アプリは自分の上へ
// 別シーンをPushしないので実害は無いが、将来詳細表示を別シーンに
// 分けるような変更をする場合は思い出すこと。
class DictScene : public Scene {
    private:
        Button* back_button       = nullptr;
        // Textboxはコード量削減のため明示インスタンス化した型しか使えない
        // (Textbox.cpp参照)。PICO_STR_LLは一般的なテキスト入力欄で
        // 既に使われている型なのでこれに合わせる
        Textbox<PICO_STR_LL>* search_box = nullptr;
        Button* search_button     = nullptr;
        Label<PICO_STR_L>* status_label = nullptr;
        ScrollList* result_list   = nullptr;
        // 表示用語句だけの見出し(スクロールしない固定行)。太字マークアップ
        // (**term**)を使うが、表示用語句(2列目)は実データを見ても
        // マークアップ記号を含まない(検索用語句・説明と違い機械的に生成された
        // 短い見出し語のため)ので、ここだけは自動装飾を有効なままにしてよい
        Label<PICO_STR_L>* detail_title = nullptr;
        // 説明文の長さは実データでかなりばらつく(短い1文〜1KiB近い長文まで)。
        // 高さを決め打ちして溢れた分を切り詰める(以前の実装)と、長い説明の
        // 途中で見えなくなってしまう。ScrollContainerで包んでスクロール可能に
        // することで、決め打ちの高さを持たずに全文へたどり着けるようにする
        // (detail_scrollが親でdetail_labelはその子。所有権はdetail_scroll側)。
        //
        // **説明文(3列目)は辞書側の自由記述で、"~"等のマークアップ記号として
        // 解釈されると困る文字を普通に含む**(実データで836行が該当。例:
        // 「《the ~》」「…でも~でもある」のような辞書独自の表記)。
        // detail_labelはsetDisableAutoTextDecoration(true)でマークアップ解釈
        // そのものを止め、常に生テキストとして表示する。
        ScrollContainer* detail_scroll = nullptr;
        Label<PICO_STR_2KiB>* detail_label = nullptr;

        WordDictionary dict_;
        // dict_.hit()のうち、まだresult_listへ積んでいない分を判別するための
        // 反映済み件数(count()は増える一方なので差分はこれだけで分かる)
        int shown_count_ = 0;

        // Pop()で戻ってきた時に検索語を復元するための退避先(ウィジェットは
        // onExit()で解放されるため、シーンのメンバ側に持っておく必要がある)
        FixedString<PICO_STR_LL> saved_query_;

        constexpr static int MARGIN = 4;
        constexpr static int SEARCH_ROW_H = 22;
        // 「戻る」はアイコン(IconID::ArrowLeft, 16px)のみのボタンにして
        // 検索欄と同じ行へ置く。以前はテキストの「戻る」ボタンだけで1行
        // (BACK_BUTTON_H+MARGIN=24px)使っていたが、そのぶんを詳細欄(説明文)へ回す
        constexpr static int BACK_BUTTON_W = 24;
        constexpr static int SEARCH_BUTTON_W = 40;
        // Button::getLocalRect()はsetW()で指定した幅に
        // _3D_PIX_LEN(2)+1を必ず足して描く(setAllowTextSpacing(false)で
        // 文字間の余白ぶんは消せるが、立体の縁取りぶんは消せない)。
        // ここを見込んでおかないとボタンの右端が画面をはみ出す
        // (--shotで実際にはみ出すのを確認して気付いた)。戻る/検索どちらの
        // ボタンもsetAllowTextSpacing(false)にするのでこの分だけ共通で見込む
        constexpr static int BUTTON_OVERHEAD = 3;
        constexpr static int STATUS_H = 16;
        constexpr static int LIST_H = 108;
        constexpr static int DETAIL_TITLE_H = 16;
        constexpr static int DETAIL_PADDING = 2;
        // ScrollContainer::SCROLL_Lはprivateなので数値をここで見込む。
        // detail_labelのmaxWidth計算にだけ使う(ずれても表示が数px余る/詰まる
        // だけで、ScrollContainer自身の当たり判定やスクロールバー位置は
        // 向こうの定数をそのまま使うので壊れない)
        constexpr static int DETAIL_SCROLLBAR_W = 15;

        void startSearch();
        void refreshResults();
        void showDetail(int index);

    public:
        const char* getName() const override { return "Dict"; }

        void onEnter() override;
        void onExit() override;
        void onUpdate() override;
};
