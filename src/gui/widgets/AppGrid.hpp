#pragma once

#include <functional>
#include "gui/widgets/Widget.hpp"
#include "functions/App_Functions.hpp"

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
class AppGrid : public Widget {
    private:
        static constexpr int kCols      = 3;
        static constexpr int kPadding   = 6;  // グリッド外周の余白
        static constexpr int kGap       = 6;  // タイル同士の間隔
        static constexpr int kTileH     = 58;
        static constexpr int kIconPx    = 32;
        static constexpr IconSize kIconSize = IconSize::Px32;
        static constexpr int kLabelGap  = 2;  // アイコンと名前の間隔

        int page_ = 0;

        // 押下中のタイルのアプリindex(-1=なし)。押し込み表示と、
        // 離した時にどれを起動するかの記録を兼ねる
        int pressed_index_ = -1;

        std::function<void(int app_index)> on_launch_ = nullptr;

        int tileW() const;
        int rowsPerPage() const;

        // ページ内スロット番号(0始まり、行優先)からタイルのローカル矩形を得る
        Rect tileRect(int slot) const;

    public:
        AppGrid(int16_t x, int16_t y, int16_t w, int16_t h){
            this->l_rect = {x, y, w, h};
        }

        WidgetType getWidgetType() const override { return WidgetType::AppGrid; }
        WidgetTools::RenderMode getRenderMode() const override { return WidgetTools::OPAQUE; }

        void render() override;
        void causeOnPressStart() override;
        void causeOnPressEnd() override;

        // 1ページに並ぶタイル数
        int tilesPerPage() const;
        // 総ページ数(アプリが0件でも1を返す)
        int pageCount() const;

        //Widget基底にはサイズ変更が無いので、GridContainerと同じく自前で持つ。
        //1ページに入るタイル数が変わるため、ページ位置を丸め直す
        void setW(int w);
        void setH(int h);

        int getPage() const { return page_; }
        void setPage(int page);
        bool nextPage();
        bool prevPage();

        // ローカル座標にあるタイルのアプリindexを返す。
        // タイルの外、または空きスロットなら-1
        int hitTile(int local_x, int local_y) const;

        void setOnLaunch(std::function<void(int app_index)> callback){
            this->on_launch_ = callback;
        }
};
