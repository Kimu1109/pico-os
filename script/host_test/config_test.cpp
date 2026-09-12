// 設定ファイル(key=value)の読み書きを検証するテスト。
//
// PICO_Config::SetValue() は一時ファイルへ書き出してから差し替える方式なので、
// 「コメントと行順が保たれるか」「重複キーが正しく畳まれるか」「往復できるか」を押さえる。
#include "functions/Config_Functions.hpp"
#include "functions/Log_Functions.hpp"
#include "OS_Data.hpp"
#include <cstdio>
#include <string>

void LogFunctions::Log(LogType, const char*, ...){}
void LogFunctions::Setup(){}
void LogFunctions::Update(){}
void LogFunctions::Flush(){}

static int failures = 0;
static void check(bool cond, const char* label){
    printf("%s %s\n", cond ? "[ OK ]" : "[FAIL]", label);
    if(!cond) failures++;
}
static void eqStr(const std::string& actual, const std::string& expected, const char* label){
    const bool ok = (actual == expected);
    printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", label);
    if(!ok){
        printf("       実測: [%s]\n", actual.c_str());
        printf("       期待: [%s]\n", expected.c_str());
        failures++;
    }
}

static const char* kPath = "cfg/sys.ini";

static void setFile(const std::string& body){ HostSd::files[kPath] = body; }
static std::string getFile(){ return HostSd::files.count(kPath) ? HostSd::files[kPath] : "(なし)"; }

// キーを1つ読み出す(見つからなければ空文字)
static std::string readKey(const char* key){
    std::string found;
    PICO_Config::ParseFile(kPath, [&](const char* k, const char* v){
        if(strcmp(k, key) == 0) found = v; //後勝ち
    });
    return found;
}

int main(){
    // ---- 既存キーの書き換え: コメントと行順が保たれる ----
    {
        setFile("# システム設定\nrun-test = false\nssid=home-ap\n\n# 末尾のコメント\n");
        check(PICO_Config::SetValue(kPath, "run-test", "true"), "既存キーの書き換えが成功する");
        eqStr(getFile(),
              "# システム設定\nrun-test=true\nssid=home-ap\n\n# 末尾のコメント\n",
              "コメント行と行順が保たれる");
        eqStr(readKey("run-test"), "true", "書き換えた値を読み戻せる");
        eqStr(readKey("ssid"), "home-ap", "他のキーは影響を受けない");
    }

    // ---- 無いキーは末尾へ追記 ----
    {
        setFile("a=1\n");
        check(PICO_Config::SetValue(kPath, "b", "2"), "新規キーの追加が成功する");
        eqStr(getFile(), "a=1\nb=2\n", "新規キーは末尾へ追記される");
    }

    // ---- 最終行に改行が無いファイル ----
    {
        setFile("a=1");
        PICO_Config::SetValue(kPath, "b", "2");
        eqStr(getFile(), "a=1\nb=2\n", "最終行に改行が無くても連結しない");
    }

    // ---- ファイルが存在しない ----
    {
        HostSd::files.erase(kPath);
        check(PICO_Config::SetValue(kPath, "first", "1"), "ファイルが無ければ新規作成する");
        eqStr(getFile(), "first=1\n", "新規作成した内容");
    }

    // ---- 重複キーは1行に畳まれる ----
    // 読み込み側が後勝ちなので、重複を残すと書き換えた値が上書きされてしまう
    {
        setFile("k=old1\nother=x\nk=old2\n");
        PICO_Config::SetValue(kPath, "k", "new");
        eqStr(getFile(), "k=new\nother=x\n", "重複キーは最初の位置に畳まれる");
        eqStr(readKey("k"), "new", "畳んだ後に読み戻すと新しい値になる");
    }

    // ---- 往復(型つき) ----
    {
        setFile("");
        char buf[32];
        PICO_Config::SetValue(kPath, "flag", PICO_Config::ConfigValue::FromBool(true));
        PICO_Config::ConfigValue::FromInt(-42, buf, sizeof(buf));
        PICO_Config::SetValue(kPath, "count", buf);
        PICO_Config::ConfigValue::FromFloat(1.5f, buf, sizeof(buf));
        PICO_Config::SetValue(kPath, "ratio", buf);

        bool b = false; int i = 0; float f = 0.0f;
        check(PICO_Config::ConfigValue::AsBool(readKey("flag").c_str(), b) && b == true,
              "bool: 書いた値を読み戻せる");
        check(PICO_Config::ConfigValue::AsInt(readKey("count").c_str(), i) && i == -42,
              "int: 書いた値を読み戻せる");
        check(PICO_Config::ConfigValue::AsFloat(readKey("ratio").c_str(), f) && f == 1.5f,
              "float: 書いた値を読み戻せる");
    }

    // ---- 不正な入力は拒否する ----
    {
        setFile("a=1\n");
        check(!PICO_Config::SetValue(kPath, "", "x"), "空のキーは拒否する");
        char longKey[PICO_Config::kConfigMaxKeyLen + 8];
        memset(longKey, 'k', sizeof(longKey) - 1);
        longKey[sizeof(longKey) - 1] = '\0';
        check(!PICO_Config::SetValue(kPath, longKey, "x"), "長すぎるキーは拒否する");
        eqStr(getFile(), "a=1\n", "拒否された場合は元ファイルが変わらない");
    }

    // ---- 一時ファイルを残さない ----
    {
        setFile("a=1\n");
        PICO_Config::SetValue(kPath, "a", "2");
        check(HostSd::files.count(std::string(kPath) + ".tmp") == 0, "一時ファイルが残らない");
    }

    printf("\n%s (failures=%d)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
