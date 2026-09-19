#pragma once

#include "Arduino.h"

// 1回のTask::update()の中で「一定時間だけ働いて、残りは次のフレームへ回す」ための
// 土台(CLAUDE.md「Lua着手前の受け皿の状態」の「実行時間の制御」に対応)。
//
// `loop()`は単純なポーリングなので、update()が長時間戻らないとタッチごと画面が
// 固まる。重い/無限ループしうる処理(将来のLuaスクリプト実行はその代表)を
// Task化する際、update()の中の作業ループをこれで区切ることで「時間で必ず戻る」
// ことを保証できる。
//
// 使い方: update()内の作業ループの継続条件にShouldContinue()を足す。
//   void update() override {
//       StepBudget budget(kStepBudgetUs);
//       while(budget.ShouldContinue() && hasMoreWork()){
//           doOneUnitOfWork();
//       }
//       // 時間切れで抜けた場合、続きは次のupdate()呼び出しへ持ち越す
//       // (途中状態はメンバ変数として自前で保持すること)
//   }
//
// 命令単位の制御(lua_sethookでNバイトコードごとに打ち切る等)はここでは扱わない。
// これはあくまで「時間で区切る」共通部品で、lua_Stateを実際に扱うLuaTask側が
// 必要に応じてこれと組み合わせるか、独自のフック粒度を足すかを選ぶ話になる。
class StepBudget {
    public:
        // budget_us: このStepBudgetが許容する経過時間(マイクロ秒)。
        // 生成した時点(=作業ループに入る直前)を起点として計る。
        explicit StepBudget(uint32_t budget_us) : budget_us_(budget_us), start_us_(micros()) {}

        // まだ予算内かどうか。作業ループはこれがtrueの間だけ続けてよい。
        // micros()のオーバーフロー(約71分でラップする)はunsigned算術の
        // 差分計算で自然に吸収される(millis()版のオーバーフロー対策と同じ考え方)。
        bool ShouldContinue() const {
            return (uint32_t)(micros() - start_us_) < budget_us_;
        }

        // ここまでに使った時間(マイクロ秒)。ログや計測に使う
        uint32_t Elapsed() const {
            return (uint32_t)(micros() - start_us_);
        }

    private:
        uint32_t budget_us_;
        uint32_t start_us_;
};
