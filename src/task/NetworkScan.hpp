#pragma once

#include "task/Task.hpp"
#include "WiFi.h"

class NetworkScan : public Task {

    private:
        unsigned long start;

    public:
        NetworkScan(){
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
            start = millis();
        }

        void update() override {
            if(millis() - start > 1000 * 10){
                this->status = TaskTools::FAILED;
            }

            int count = WiFi.scanComplete();
            if (count >= 0) {
                for (int i = 0; i < count; i++) {
                    const char* ssid = WiFi.SSID(i);
                    int32_t rssi = WiFi.RSSI(i);
                    Serial.printf("ssid: %s, rssi: %d\n", ssid, rssi);   
                }
                this->status = TaskTools::SUCCESS;
            } else if (count == -2) {
                this->status = TaskTools::FAILED;
            }
        }
};