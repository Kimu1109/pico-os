#include "apps/reader/Reader_Book.hpp"

ReaderBook::ReaderBook(Aozora::Source& src) : reader_(src, info_) {
    Aozora::Analyze(src, info_);
    cur_.pos = info_.body_start;
    State first;
    first.pos = info_.body_start;
    index_.reset(first);
}

void ReaderBook::setParams(const ReaderLayout::Params& p){
    const bool changed = (p != params_);
    params_ = p;
    if(!changed){
        render();
        return;
    }
    State first;
    first.pos = info_.body_start;
    index_.reset(first);
    toc_.clear();
    goToPos(cur_.pos);
}

void ReaderBook::render(){
    next_ = ReaderLayout::LayoutPage(reader_, cur_, params_, &page_, &at_end_, nullptr);
    page_no_valid_ = false;
}

void ReaderBook::indexUntil(uint32_t pos){
    while(!index_.covers(pos)) index_.step(reader_, params_, &toc_, 16);
}

void ReaderBook::goTo(const State& s){
    cur_ = s;
    if(cur_.pos < info_.body_start){
        cur_ = State();
        cur_.pos = info_.body_start;
    }
    if(cur_.pos > reader_.size()) cur_.pos = reader_.size();
    int n;
    State start;
    if(index_.find(cur_.pos, reader_, params_, n, start)) cur_ = start;
    render();
}

void ReaderBook::goToPos(uint32_t pos){
    if(pos >= reader_.size()) pos = reader_.size() > 0 ? reader_.size() - 1 : 0;
    if(pos < info_.body_start) pos = info_.body_start;
    indexUntil(pos);
    int n;
    State start;
    if(index_.find(pos, reader_, params_, n, start)){
        cur_ = start;
    }else{
        cur_ = State();
        cur_.pos = info_.body_start;
    }
    render();
}

void ReaderBook::goToRatio(float r){
    if(r < 0) r = 0;
    if(r > 1) r = 1;
    const uint32_t body = reader_.size() > info_.body_start ? reader_.size() - info_.body_start : 0;
    goToPos(info_.body_start + (uint32_t)(r * (float)body));
}

bool ReaderBook::next(){
    if(at_end_ || next_.pos <= cur_.pos) return false;
    cur_ = next_;
    render();
    return true;
}

bool ReaderBook::prev(){
    if(atStart()) return false;
    indexUntil(cur_.pos);
    int n;
    State start;
    if(!index_.find(cur_.pos, reader_, params_, n, start)) return false;
    if(start.pos < cur_.pos){
        // 今のページが索引のページの途中から始まっている(しおり・組み方の変更): そのページの頭へ
        cur_ = start;
    }else{
        if(n == 0 || !index_.startOf(n - 1, reader_, params_, start)) return false;
        cur_ = start;
    }
    render();
    return true;
}

int ReaderBook::pageNumber(){
    if(page_no_valid_) return page_no_;
    State start;
    if(index_.find(cur_.pos, reader_, params_, page_no_, start)){
        page_no_valid_ = true;
        return page_no_;
    }
    return -1;
}

bool ReaderBook::indexStep(int pages){
    return index_.step(reader_, params_, &toc_, pages);
}

float ReaderBook::progress(){
    if(at_end_) return 1.0f;
    if(index_.complete() && index_.pageCount() > 0){
        const int n = pageNumber();
        if(n >= 0) return (float)(n + 1) / (float)index_.pageCount();
    }
    const uint32_t body = reader_.size() > info_.body_start ? reader_.size() - info_.body_start : 0;
    if(body == 0) return 1.0f;
    return (float)(next_.pos - info_.body_start) / (float)body;
}
