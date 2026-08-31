#pragma once

#include "task/Task.hpp"
#include "vector"
#include "functions/Log_Functions.hpp"

namespace PICO_Task {
    inline std::vector<Task*> tasks;

    inline void Setup(){
        tasks.reserve(16);
        LOG_SYS_OK("Task setup has succeeded!");
    }

    inline void Add(Task* task){
        tasks.push_back(task);
    }

    inline void Update(){
        for(int i = 0; i < tasks.size(); i++){
            tasks[i]->update();
            if(tasks[i]->getStatus() != TaskTools::PROCESSING){
                LOG_SYS_MSG("task[%d] finished. last status : %s\n", i, TaskTools::StatusToStr(tasks[i]->getStatus()));
                tasks.erase(tasks.begin() + i);
                break;
            }
        }
    }
};