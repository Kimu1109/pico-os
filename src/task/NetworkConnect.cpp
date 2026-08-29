#include "task/NetworkConnect.hpp"
#include "functions/Network_Functions.hpp"

void NetworkConnect::update(){
    if(WiFi.status() == WL_CONNECTED){
        NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::SUCCESS;
        NTP.begin("ntp.nict.jp");
        this->status = TaskTools::SUCCESS;
    }

    if(millis() - start > 1000 * 10){
        switch(WiFi.status()){
            case WL_NO_SSID_AVAIL:
                NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::SSID_NOT_FOUND;
                break;
            case WL_CONNECT_FAILED:
                NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::FAILED;
                break;
            case WL_DISCONNECTED:
            case WL_CONNECTION_LOST:
            default:
                NetworkFunctions::currentStatus = NetworkFunctions::NetStatus::TIMEOUT;
                break;
        }
        this->status = TaskTools::FAILED;
    }
}