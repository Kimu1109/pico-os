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

class Task {

    protected:
        TaskTools::Status status = TaskTools::Status::PROCESSING;

    public:
        virtual void update() = 0;
        TaskTools::Status getStatus(){
            return this->status;
        }
};