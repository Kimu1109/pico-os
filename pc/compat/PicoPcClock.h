#pragma once
// PC/Web/ホストテスト用の「本体の時計」。
//
// 実機は settimeofday() で時計を合わせる(TimeFunctions::SetClock())。PCで同じことをすると
// 母艦の時計そのものを書き換えてしまう(rootで動いていると本当に変わる)ので、ここでは
// 母艦の時計からの「ずれ」だけを持つ。
// ネイティブのPCビルドは main_pc.cpp が time() を差し替えてこのずれを足すので、
// time(nullptr) を使う全ての所(時計・アラーム・通知・TLSの有効期限)がずれた時刻を見る。
// Webビルドとホストテストは time() を差し替えないので、ずれは記録されるだけで効かない。
#include <atomic>
#include <ctime>

namespace PicoPcClock {
    inline std::atomic<long long> offset_sec{0};

    // 母艦の今の時刻(ずれを足していないもの。time()を差し替えていても影響されない)
    inline long long RealNow(){
        timespec ts{};
        clock_gettime(CLOCK_REALTIME, &ts);
        return (long long)ts.tv_sec;
    }
    inline long long Now(){ return RealNow() + offset_sec.load(); }
    // 本体の時計を t(エポック秒)にする
    inline bool Set(long long t){
        offset_sec.store(t - RealNow());
        return true;
    }
}
