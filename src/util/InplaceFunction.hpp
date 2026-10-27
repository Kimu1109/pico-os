// src/util/InplaceFunction.hpp
//
// ヒープを使わない std::function の代わり。ウィジェットのコールバック(on_press_end 等)用。
//
// - 呼び出す物(ラムダ・関数ポインタ)を自分の中の固定長の箱(既定はポインタ3つぶん)へ置く。
//   箱に収まらない物は**コンパイルエラー**になる(std::function は黙ってヒープへ逃がすので、
//   キャプチャを1つ足しただけでウィジェット1個ごとの確保が増えることに気付けなかった)。
// - 大きさは std::function と同じ(32bitで16B、64bitで32B)。中身の種類ごとの操作表を
//   1本のポインタで指すので、箱を広く取れる(std::function の小バッファはポインタ2つぶん)。
// - std::function と同じく nullptr の代入・比較、bool への変換、コピーができる。
//
// 箱に収まらないと言われたら: ①キャプチャを this と小さな値だけにする(大きな値はメンバへ移す)、
// ②どうしても大きいものは Capacity を広げた別の型にする(既定は広げない。ウィジェット全部に効くため)。
//
// 箱はポインタ3つぶんなので、実機(32bit)では12B、PC(64bit)では24B。ポインタと int を混ぜたキャプチャは
// PCで収まっても実機で収まらないことがある([this, d, int, int] 等)。実機のビルド(CI)でも必ず確かめる。
#pragma once
#include <stddef.h>
#include <new>
#include <type_traits>
#include <utility>

template<typename Signature, size_t Capacity = 3 * sizeof(void*)>
class InplaceFunction;

template<typename R, typename... Args, size_t Capacity>
class InplaceFunction<R(Args...), Capacity> {
    // 中身の種類ごとの操作。種類ごとに static な表が1つだけできる
    struct Ops {
        R (*invoke)(void* self, Args... args);
        void (*copy)(void* dst, const void* src);  // dst へ作る(dst は空)
        void (*destroy)(void* self);
    };

    template<typename Fn>
    struct OpsFor {
        static R Invoke(void* self, Args... args) {
            return (*static_cast<Fn*>(self))(std::forward<Args>(args)...);
        }
        static void Copy(void* dst, const void* src) {
            ::new (dst) Fn(*static_cast<const Fn*>(src));
        }
        static void Destroy(void* self) {
            static_cast<Fn*>(self)->~Fn();
        }
        static constexpr Ops table = { &Invoke, &Copy, &Destroy };
    };

    alignas(void*) unsigned char storage_[Capacity];
    const Ops* ops_ = nullptr;

    template<typename Fn>
    using IsSelf = std::is_same<typename std::decay<Fn>::type, InplaceFunction>;

public:
    InplaceFunction() = default;
    InplaceFunction(decltype(nullptr)) {}

    template<typename Fn, typename = typename std::enable_if<!IsSelf<Fn>::value>::type>
    InplaceFunction(Fn&& f) {
        assignCallable(std::forward<Fn>(f));
    }

    InplaceFunction(const InplaceFunction& other) { copyFrom(other); }
    // 箱の中身は移せないものもあるので、移動もコピーとして扱う(中身はどれも小さい)
    InplaceFunction(InplaceFunction&& other) { copyFrom(other); }

    ~InplaceFunction() { reset(); }

    InplaceFunction& operator=(const InplaceFunction& other) {
        if (this != &other) { reset(); copyFrom(other); }
        return *this;
    }
    InplaceFunction& operator=(InplaceFunction&& other) {
        if (this != &other) { reset(); copyFrom(other); }
        return *this;
    }
    InplaceFunction& operator=(decltype(nullptr)) { reset(); return *this; }

    template<typename Fn, typename = typename std::enable_if<!IsSelf<Fn>::value>::type>
    InplaceFunction& operator=(Fn&& f) {
        reset();
        assignCallable(std::forward<Fn>(f));
        return *this;
    }

    explicit operator bool() const { return ops_ != nullptr; }

    // 空のまま呼ぶのは呼び出し側の誤り(std::function と違って例外は投げられない)。
    // 既存のコードは全部 if (cb) cb(); の形で呼んでいる
    R operator()(Args... args) const {
        return ops_->invoke(const_cast<unsigned char*>(storage_), std::forward<Args>(args)...);
    }

    friend bool operator==(const InplaceFunction& f, decltype(nullptr)) { return !f; }
    friend bool operator==(decltype(nullptr), const InplaceFunction& f) { return !f; }
    friend bool operator!=(const InplaceFunction& f, decltype(nullptr)) { return (bool)f; }
    friend bool operator!=(decltype(nullptr), const InplaceFunction& f) { return (bool)f; }

private:
    void reset() {
        if (ops_) { ops_->destroy(storage_); ops_ = nullptr; }
    }

    void copyFrom(const InplaceFunction& other) {
        if (other.ops_) { other.ops_->copy(storage_, other.storage_); ops_ = other.ops_; }
    }

    template<typename Fn>
    void assignCallable(Fn&& f) {
        using T = typename std::decay<Fn>::type;
        static_assert(sizeof(T) <= Capacity,
            "InplaceFunction: キャプチャが箱に収まりません(thisと小さな値だけにするか、大きな値はメンバへ移す)");
        static_assert(alignof(T) <= alignof(void*), "InplaceFunction: 整列の要求が大きすぎます");
        static_assert(std::is_copy_constructible<T>::value, "InplaceFunction: コピーできない物は入れられません");
        // 関数ポインタの nullptr は空として扱う(std::function と同じ)
        if constexpr (std::is_pointer<T>::value) {
            if (f == nullptr) return;
        }
        ::new (static_cast<void*>(storage_)) T(std::forward<Fn>(f));
        ops_ = &OpsFor<T>::table;
    }
};
