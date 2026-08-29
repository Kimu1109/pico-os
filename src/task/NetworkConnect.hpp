#pragma once

#include "task/Task.hpp"
#include "WiFi.h"

class NetworkConnect : public Task {
    private:
        unsigned long start;
    public:
        NetworkConnect(const char* SSID, const char* password){
            start = millis();
            WiFi.beginNoBlock(SSID, password);
        }
        void update() override;
};