#pragma once

#include "apps/reader/Aozora_Text.hpp"
#include "apps/reader/Reader_Layout.hpp"

// 開いている本1冊(読書アプリの中身。画面を持たないのでホストテストで回せる)。
//
// 今のページの頭(State)と、組んだ今のページ(Page)、ページの索引(PageIndex)、目次(Toc)を持つ。
// 索引は開いた直後は空で、画面が indexStep() を毎フレーム少しずつ呼んで作る(長編でも開くのを待たせない)。
// 索引が要る操作(前のページ・ページ番号・目次や割合で飛ぶ)は、足りなければその場で必要なところまで作る。
//
// 大きさは約16KB(索引8KB・目次4KB・今のページ3KB)。本を開いている間だけ new する。
class ReaderBook {
    public:
        using State = ReaderLayout::State;

        // src は閉じるまで呼び出し側が持ち続けること
        explicit ReaderBook(Aozora::Source& src);
        ReaderBook(const ReaderBook&) = delete;
        ReaderBook& operator=(const ReaderBook&) = delete;

        const Aozora::Info& info() const { return info_; }
        uint32_t size() const { return reader_.size(); }

        // 組み方(幅・行数・ルビ・文字の幅)を決める。変わったら索引を作り直し、今の位置を含むページへ揃える
        void setParams(const ReaderLayout::Params& p);
        const ReaderLayout::Params& params() const { return params_; }

        // その位置から表示する(しおり・続きから)。索引がそこまであればページの頭へ揃える
        void goTo(const State& s);
        // 位置を含むページへ(目次)。索引が無ければその場で作る
        void goToPos(uint32_t pos);
        // 本の中の割合(0〜1)のページへ
        void goToRatio(float r);
        bool next();
        bool prev();

        const State& current() const { return cur_; }
        const ReaderLayout::Page& page() const { return page_; }
        bool atEnd() const { return at_end_; }
        // 今のページの終わり(次のページの頭の位置)
        uint32_t endPos() const { return at_end_ ? reader_.size() : next_.pos; }
        bool atStart() const { return cur_.pos <= info_.body_start; }

        // 今のページの番号(0から)。索引がまだ届いていなければ -1
        int pageNumber();
        // 数え終わったページ数(indexComplete() なら全ページ数)
        int pageCount() const { return index_.pageCount(); }
        bool indexComplete() const { return index_.complete(); }
        // 索引を最大 pages ページぶん進める。終わっていれば true
        bool indexStep(int pages);
        // 本の中のどこまで読んだか(0〜1)
        float progress();

        const ReaderLayout::Toc& toc() const { return toc_; }

    private:
        Aozora::Info info_;
        Aozora::Reader reader_;
        ReaderLayout::Params params_;
        ReaderLayout::PageIndex index_;
        ReaderLayout::Toc toc_;
        ReaderLayout::Page page_;
        State cur_;
        State next_;
        bool at_end_ = false;
        int page_no_ = -1;
        bool page_no_valid_ = false;

        void render();
        void indexUntil(uint32_t pos);
};
