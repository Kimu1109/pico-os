// StepBudget(Task::update()内の作業ループを時間で区切る土台)の検証。
//
// script/host_test/stubs/Arduino.hのmicros()は常に0を返すダミーのため、実際に
// 時間切れになることを確かめるにはpc/compat/Arduino.h(実時間のmicros())を使う
// (run.shの他のテストと違いstubs/ではなくpc/compat/を-Iで渡す)。
#include "task/StepBudget.hpp"
#include <cstdio>
#include <thread>
#include <chrono>

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}

int main(){
    // budget=0は生成した瞬間に予算切れ扱いになる
    {
        StepBudget budget(0);
        check(!budget.ShouldContinue(), "budget=0は即座にfalse");
    }

    // 予算内はtrueのまま
    {
        StepBudget budget(50 * 1000); // 50ms
        check(budget.ShouldContinue(), "予算内はtrue");
    }

    // 予算を使い切ると自動的にfalseへ切り替わる(実時間で確認)
    {
        StepBudget budget(5 * 1000); // 5ms
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(!budget.ShouldContinue(), "予算超過後はfalse");
        check(budget.Elapsed() >= 5000, "Elapsed()が予算以上を報告する");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
