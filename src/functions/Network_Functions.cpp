#include "functions/Network_Functions.hpp"
#include "functions/Task_Functions.hpp"
#include "functions/Config_Functions.hpp"
#include "task/NetworkConnect.hpp"
#include "task/NetworkScan.hpp"
#include "storage/SD_Path.hpp"

void NetworkFunctions::Setup(){
    char ssid[33] = "";
    char password[65] = "";
    bool is_ok = PICO_Config::ParseFile(PICO_Path::FILE::SYS_NETWORK_CFG,
        [&](const char* key, const char* value){
            if(strcmp(key, "ssid") == 0){
                strncpy(ssid, value, sizeof(ssid) - 1);
            }else if(strcmp(key, "password") == 0){
                strncpy(password, value, sizeof(password) - 1);
            }
        }
    );
    if(is_ok){
        if(ssid[0] != '\0' && password[0] != '\n'){
            ConnectWiFiAsync(ssid, password);
        }
    }
};

Task* NetworkFunctions::ConnectWiFiAsync(const char* ssid, const char* password){
    NetworkConnect* task = new NetworkConnect(ssid, password);
    PICO_Task::Add(task);
    return task;
};

Task* NetworkFunctions::ScanAsync(){
    NetworkScan* task = new NetworkScan();
    PICO_Task::Add(task);
    return task;
};