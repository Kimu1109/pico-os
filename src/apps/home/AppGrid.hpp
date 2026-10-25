#pragma once

#include <functional>
#include "gui/widgets/Widget.hpp"
#include "services/App_Functions.hpp"
#include "apps/home/Home_Layout.hpp"

class HomeWidget;

// 登録済みアプリをタイル状に並べるランチャ用ウィジェット。
//
// タイルごとに子ウィジェットを作らず、render()でアイコンと名前を直接描き、
// タップ位置から対象のタイルを逆算する(ColorDialogの色グリッドと同じ方式)。
// こうする理由は2つ:
//   - アプリ数ぶんのウィジェット(アイコン+ラベルで2倍)を抱えずに済む
//   - WidgetFunctions::HitTest()は子から先に判定するため、タイルを
//     Icon+Labelの親ウィジェットとして作ると子がタップを奪ってしまう
//
// 画面に入りきらない数のアプリはページ送りで見せる。
//
// フォルダ(AppFunctions のフォルダ)はアプリと同じ1マスのタイルで、中の先頭4つのアイコンを縮めて描く。
// 押すとグリッドがそのフォルダの中身に入れ替わる(openFolder。閉じるのは closeFolder、キーの戻る)。
//
// ホーム画面のウィジェット(時計・音楽・予定・TODO)もアプリのタイルと同じ並びに混ざる
// (HomeLayout が並びとマス目への詰め方を持ち、描き方は HomeWidget)。ウィジェットも
// 子ウィジェットを作らず、マス目の矩形を渡して直接描かせる。
class AppGrid : public Widget {
    private:
        // 2列にしているのは、3列(タイル幅72px=日本語4文字)だと大半のアプリ名が
        // はみ出してしまうため。2列なら111px=6文字/行あり、下の2行表示と合わせて
        // 12文字程度まで収まる。1ページ6個に減るが、名前が読めないよりはよい
        static constexpr int kCols      = HomeLayout::kCols;
        static constexpr int kPadding   = 6;  // グリッド外周の余白
        static constexpr int kGap       = 6;  // タイル同士の間隔
        static constexpr int kTileH     = 78;
        static constexpr int kIconPx    = 48;
        static constexpr IconSize kIconSize = IconSize::Px48;
        static constexpr int kLabelGap  = 2;  // アイコンと名前の間隔

        // 名前は最大この行数まで折り返す。長いアプリ名を1行に押し込むと
        // 途中で切れて何のアプリか分からなくなるため
        static constexpr int kNameLines = 2;
        // 名前1件ぶんの最大バイト数(これを超える分は切り詰める)
        static constexpr int kMaxNameBytes = 64;

        int page_ = 0;
        // 開いているフォルダ(-1=トップ)。中が空になったら ensureLayout() がトップへ戻す
        mutable int folder_ = -1;

        // ---- 並べた結果(HomeLayout::Compute)。並び・大きさ・行数が変わったときだけ作り直す ----
        mutable HomeLayout::Item items_[HomeLayout::kMaxItems];
        mutable int item_count_ = 0;
        mutable int pages_ = 1;
        mutable uint32_t layout_sig_ = 0;
        // 合わせて作り直す(並び・隠した印・登録簿・ウィジェット・行数のどれかが変わっていたら)
        void ensureLayout() const;
        uint32_t layoutSignature() const;
        // 今のページの、ローカル座標にある項目(items_ の番号。無ければ-1)
        int hitItem(int local_x, int local_y) const;
        Rect itemRect(const HomeLayout::Item& it) const;
        Rect itemScreenRect(const HomeLayout::Item& it) const;
        HomeWidget* widgetOf(const HomeLayout::Item& it) const;

        // ウィジェットの見た目の印(stamp)。変わったものだけを描き直す
        uint32_t stamps_[HomeLayout::kMaxItems] = {};
        uint32_t stamps_page_sig_ = 0;

        // 押している項目(items_ の番号、-1=なし)と、ウィジェットの中で押しているボタン(-1=ボタンの外)
        int pressed_item_ = -1;
        int pressed_button_ = -1;
        // 押し始めた時刻と、長押しを知らせ済みか(知らせたら離しても起動しない)
        unsigned long press_ms_ = 0;
        bool long_pressed_ = false;
        // 長押しした場所(項目の外なら並びの最後へ足す)
        bool long_on_empty_ = false;

        std::function<void(int app_index)> on_launch_ = nullptr;

        // キー/コントローラーで選んでいる項目(items_ の番号)。枠はフォーカスの枠が出ている間だけ描く
        int focus_item_ = 0;
        // 今のページの最初の項目(無ければ-1)
        int firstItemOnPage(int page) const;
        void clampFocus();
        void activate(int item);

        int tileW() const;

        // 名前をタイル幅に合わせて最大kNameLines行で中央揃え描画する。
        // DrawPlain()は折り返さないので、UTF-8の文字境界で切って自前で行を分ける
        void drawName(const char* name, int x, int y, int w, int color);

        // タイルのアイコンを描く。entry.icon_fileが設定されていればSDから.pimgを
        // 読んで描き(失敗時はentry.iconへフォールバック)、空ならentry.icon
        // (組み込みIconID)をそのまま描く。x,yはkIconPx四方のアイコン枠の左上
        void drawIcon(const AppEntry& entry, int x, int y, int color);
        // フォルダのタイルのアイコン(kIconPx四方の角の丸い枠に、中の先頭4つを16pxで)
        void drawFolderIcon(int folder, int x, int y, bool pressed);
        // アプリのアイコンを box 四方へ縮めて描く(.pimgは間引き、組み込みは16px)
        void drawSmallIcon(const AppEntry& entry, int x, int y, int box, int color);

    public:
        AppGrid(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
        }

        // ページ内スロット番号(0始まり、行優先)から1マスのローカル矩形を得る。
        // テストから配置と当たり判定の整合を確かめられるよう公開している
        Rect tileRect(int slot) const;
        // マス目(列,行,幅,高さ)のローカル矩形
        Rect cellRect(int col, int row, int w, int h) const;

        WidgetType getWidgetType() const override { return WidgetType::AppGrid; }
        bool focusableByDefault() const override { return true; }
        bool drawsOwnFocus() const override { return true; }
        // 矢印/十字キーで項目を選び(左右の端ではページを送る)、決定で起動する
        bool onFocusKey(FocusKey key) override;
        Rect focusRect() const override;
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }
        // 今のページにウィジェットがあれば、毎フレーム見た目の変化を見張る
        bool wantsFrameUpdate() const override;

        void render() override;
        void causeOnPressStart() override;
        void causeOnPressEnd() override;

        // 1ページのマスの数・行の数
        int tilesPerPage() const;
        int rowsPerPage() const;
        // 総ページ数(何も無くても1を返す)
        int pageCount() const;

        //Widget基底にはサイズ変更が無いので、GridContainerと同じく自前で持つ。
        //1ページに入るマスの数が変わるため、ページ位置を丸め直す
        void setW(int w);
        void setH(int h);

        int getPage() const { return page_; }
        void setPage(int page);
        bool nextPage();
        bool prevPage();

        // ローカル座標にあるタイルのアプリindex(登録簿の番号)を返す。
        // アプリのタイルの外(ウィジェット・フォルダ・空き)なら-1
        int hitTile(int local_x, int local_y) const;

        // 長押しされた項目
        struct LongPress {
            bool on_item = false;               // 偽なら空いた所(足すなら並びの最後へ)
            HomeLayout::ItemType type = HomeLayout::ItemType::App;
            int index = -1;                     // App: 登録簿の番号 / Widget: HomeLayout::placed の番号 / Folder: フォルダの番号
        };
        // 押したまま long_ms 経ったら1回だけ真を返す。返した項目は離しても起動しない。
        // ランチャ(HomeScene)が毎フレーム呼んでメニューを出す
        bool takeLongPress(unsigned long now_ms, unsigned long long_ms, LongPress& out);
        // キー/コントローラーで選んでいる項目(無ければ偽)
        bool focusedItem(HomeLayout::ItemType& type, int& index) const;
        // キー/コントローラーで選んでいるタイルのアプリindex(アプリでなければ-1)
        int focusedApp() const;
        // その項目が見えるページへ移り、選んでいる項目にする(並べ替えの後に追いかける)
        void showItem(HomeLayout::ItemType type, int index);
        void showApp(int app_index){ showItem(HomeLayout::ItemType::App, app_index); }
        // 並び・隠したアプリ・ウィジェットが変わった後に呼ぶ(ページと選んでいる項目を丸め直して描き直す)
        void layoutChanged();

        // フォルダを開く/閉じる(閉じるとトップのそのフォルダのページへ戻る)。開いているフォルダ(-1=トップ)
        bool openFolder(int folder);
        void closeFolder();
        int openedFolder() const { ensureLayout(); return folder_; }

        void setOnLaunch(std::function<void(int app_index)> callback){
            this->on_launch_ = callback;
        }
};
