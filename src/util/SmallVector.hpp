// src/util/SmallVector.hpp
//
// 先頭のN個ぶんを自分の中に持つ可変長配列(std::vector の代わり)。
//
// ウィジェットの子の一覧やLabelの行データは「たいてい数個」なのに、std::vector は
// 1個目を積んだ瞬間にヒープを叩き、増えるたびに確保し直す(1,2,4,8…)。ウィジェット1個あたりの
// 小さな確保が寿命バラバラのままヒープに散らばるので、N個までは自分の中に置いて確保を0回にする。
// N個を超えたら1回だけまとめてヒープへ移る(以後は std::vector と同じく倍々に広げる)。
//
// - 中身は memcpy で動かせる型だけ(ポインタ・数値・小さな構造体)。コンストラクタ・デストラクタは呼ばない
// - 確保に失敗したら push_back()/reserve() が false を返し、中身はそのまま(例外は投げない)
// - begin()/end() は生ポインタなので、範囲for・std::find がそのまま使える
#pragma once
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>

template<typename T, size_t N>
class SmallVector {
    static_assert(std::is_trivially_copyable<T>::value, "SmallVector: memcpyで動かせる型だけ入れられます");

    T* data_;
    size_t size_ = 0;
    size_t capacity_ = N;
    // N == 0 でも配列の大きさを0にしないための1(使わない)
    alignas(T) unsigned char inline_[sizeof(T) * (N > 0 ? N : 1)];

    T* inlineData() { return reinterpret_cast<T*>(inline_); }
    const T* inlineData() const { return reinterpret_cast<const T*>(inline_); }

public:
    SmallVector() : data_(inlineData()) {}
    ~SmallVector() { releaseHeap(); }

    SmallVector(const SmallVector& other) : data_(inlineData()) { (void)copyFrom(other); }
    SmallVector& operator=(const SmallVector& other) {
        if (this != &other) { clear(); (void)copyFrom(other); }
        return *this;
    }
    // ヒープに移っていれば中身を奪う(確保し直さない)。内側にあるならコピーする
    SmallVector(SmallVector&& other) : data_(inlineData()) { takeFrom(other); }
    SmallVector& operator=(SmallVector&& other) {
        if (this != &other) { releaseHeap(); data_ = inlineData(); capacity_ = N; size_ = 0; takeFrom(other); }
        return *this;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    // ヒープへ移ったか(テスト・計測用)
    bool onHeap() const { return data_ != inlineData(); }

    T* data() { return data_; }
    const T* data() const { return data_; }
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }

    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }
    T& back() { return data_[size_ - 1]; }
    const T& back() const { return data_[size_ - 1]; }

    // 中身を捨てる。確保した領域は持ったまま(また積むときに確保し直さない)
    void clear() { size_ = 0; }

    bool reserve(size_t n) {
        if (n <= capacity_) return true;
        T* p = static_cast<T*>(malloc(sizeof(T) * n));
        if (!p) return false;
        if (size_) memcpy(p, data_, sizeof(T) * size_);
        releaseHeap();
        data_ = p;
        capacity_ = n;
        return true;
    }

    bool push_back(const T& v) {
        if (size_ == capacity_) {
            // vの参照先が自分の中身でも壊れないよう、広げる前に写しておく
            const T copy = v;
            if (!reserve(capacity_ ? capacity_ * 2 : 4)) return false;
            data_[size_++] = copy;
            return true;
        }
        data_[size_++] = v;
        return true;
    }

    void pop_back() { if (size_) size_--; }

    // 中身をn個のvにする
    bool assign(size_t n, const T& v) {
        clear();
        if (!reserve(n)) return false;
        for (size_t i = 0; i < n; i++) data_[i] = v;
        size_ = n;
        return true;
    }

    // posの1個を消し、後ろを詰める。消した位置(=次の要素)を返す
    T* erase(T* pos) {
        const size_t i = (size_t)(pos - data_);
        if (i >= size_) return end();
        memmove(data_ + i, data_ + i + 1, sizeof(T) * (size_ - i - 1));
        size_--;
        return data_ + i;
    }

private:
    void releaseHeap() {
        if (data_ != inlineData()) free(data_);
    }

    bool copyFrom(const SmallVector& other) {
        if (!reserve(other.size_)) return false;
        if (other.size_) memcpy(data_, other.data_, sizeof(T) * other.size_);
        size_ = other.size_;
        return true;
    }

    // thisは空・内側を指している前提
    void takeFrom(SmallVector& other) {
        if (other.onHeap()) {
            data_ = other.data_;
            capacity_ = other.capacity_;
            size_ = other.size_;
            other.data_ = other.inlineData();
            other.capacity_ = N;
            other.size_ = 0;
        } else {
            if (other.size_) memcpy(data_, other.data_, sizeof(T) * other.size_);
            size_ = other.size_;
            other.size_ = 0;
        }
    }
};
