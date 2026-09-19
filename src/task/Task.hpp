#pragma once

namespace TaskTools {
    enum Status {
        PROCESSING,
        SUCCESS,
        FAILED,
    };

    inline const char* StatusToStr(Status stat){
        switch (stat)
        {
        case Status::PROCESSING:
            return "PROCESSING";
        case Status::SUCCESS:
            return "SUCCESS";
        case Status::FAILED:
            return "FAILED";
        default:
            return "UNKNOWN";
        }
    }
};

// update()は毎フレームPICO_Task::Update()から呼ばれるので、長時間戻らないと
// タッチごと画面が固まる(loop()は単純なポーリングのため)。1回のupdate()が
// どれだけ働くか予測できない処理(将来のLuaスクリプト実行が代表例)をTask化
// する場合は、作業ループを task/StepBudget.hpp のStepBudgetで区切り、
// 時間切れの続きを次のupdate()へ持ち越すこと。
class Task {

    protected:
        TaskTools::Status status = TaskTools::Status::PROCESSING;

    public:
        virtual void update() = 0;
        TaskTools::Status getStatus(){
            return this->status;
        }
};