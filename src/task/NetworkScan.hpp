#pragma once

#include "task/Task.hpp"
#include "util/FixedString.hpp"
#include "consts.hpp"
#include "WiFi.h"

// 周辺のWi-Fiを非同期でスキャンするTask。
//
// **`NetworkFunctions::ScanAsync()`はPICO_Task::Add()には乗せず、呼び出し側へ
// 所有権をそのまま渡す**(HttpGet/HttpRequestと同じ「値/生ポインタとして持ち、
// 自分のonUpdate()から毎フレームupdate()を呼ぶ」流儀)。理由:
// PICO_Task::Add()に乗せると、status()がPROCESSING以外になったその場で
// PICO_Task::Update()自身が即座にdeleteしてしまう(Task_Functions.hpp参照)。
// main.cppのloop()はSceneFunctions::Update()(=シーンのonUpdate())を
// PICO_Task::Update()より先に呼ぶため、呼び出し側が完了を検知できるのは
// 早くても次のフレームのonUpdate()であり、その時点では既に解放済みになっている
// (実際にこの順序で使ってみて踏んだバグ)。呼び出し側は毎フレームupdate()を呼び、
// getStatus()がPROCESSING以外になったらgetCount()/getResult()を読み、
// 自分でdeleteすること。
class NetworkScan : public Task {

    public:
        constexpr static int kMaxResults = 16;

        struct Result {
            FixedString<PICO_STR_M> ssid;
            int32_t rssi = 0;
        };

    private:
        unsigned long start;
        Result results_[kMaxResults];
        int count_ = 0;

        // 電波の強い順に挿入する(高々kMaxResults件なので挿入ソートで足りる。
        // CalendarScene::reload()のファイル名ソートと同じ考え方)
        void addResult(const char* ssid, int32_t rssi){
            if(!ssid || !ssid[0]) return; // 隠しSSID(空文字列)は選びようが無いので出さない
            if(this->count_ >= kMaxResults) return;

            int i = this->count_++;
            while(i > 0 && this->results_[i - 1].rssi < rssi){
                this->results_[i] = this->results_[i - 1];
                i--;
            }
            this->results_[i].ssid.assign(ssid);
            this->results_[i].rssi = rssi;
        }

    public:
        NetworkScan(){
            WiFi.scanDelete();
            WiFi.scanNetworks(true);
            start = millis();
        }

        void update() override {
            if(millis() - start > 1000 * 10){
                this->status = TaskTools::FAILED;
                return;
            }

            int count = WiFi.scanComplete();
            if (count >= 0) {
                this->count_ = 0;
                for (int i = 0; i < count; i++) {
                    this->addResult(WiFi.SSID(i), WiFi.RSSI(i));
                }
                this->status = TaskTools::SUCCESS;
            } else if (count == -2) {
                this->status = TaskTools::FAILED;
            }
        }

        int getCount() const { return this->count_; }
        const Result& getResult(int i) const { return this->results_[i]; }
};
