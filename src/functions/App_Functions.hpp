#pragma once

#include "gui/icons/icons_data.h"

class Scene;

// ランチャに並ぶアプリ1つぶんの情報。
// 静的なテーブルへ積むので、nameには必ず静的寿命の文字列(リテラル)を渡すこと。
struct AppEntry {
    const char* name = nullptr;
    IconID icon = IconID::AppBox;

    // シーンを1つ生成する。生成したシーンの所有権はSceneFunctionsへ渡る。
    // std::functionではなく素の関数ポインタにしてあるのは、
    // 登録簿を静的テーブル(確保ゼロ)のままにしたいため
    Scene* (*create)() = nullptr;
};

// アプリの登録簿。
//
// 狙いは「アプリを1つ増やすときに触る場所を1箇所にする」こと。
// 以前は HomeScene がアプリごとのボタンをメンバとしてハードコードしていたため、
// アプリを足すたびに HomeScene の .hpp と .cpp の両方を編集する必要があり、
// 標準アプリ6個+セカンダリ7個を並べると破綻する形だった。
//
// 仕組み(Register/Launch等)は App_Functions.cpp に、
// 実際に載せるアプリの一覧は App_List.cpp の Setup() にある。
// アプリを増やすときは App_List.cpp へ1行足すだけでよい。
namespace AppFunctions {
    // 登録できるアプリ数の上限。固定長配列で持つので、超えた分は警告して捨てる
    constexpr int kMaxApps = 24;

    inline AppEntry apps[kMaxApps];
    inline int app_count = 0;

    // 型Tのシーンを作るファクトリ。Register()へ &MakeScene<XxxScene> の形で渡す
    template<typename T>
    Scene* MakeScene(){ return new T(); }

    // 登録に成功したらtrue。名前/生成関数が未指定、または上限超過でfalse
    bool Register(const char* name, IconID icon, Scene* (*create)());

    int Count();

    // 範囲外なら nullptr
    const AppEntry* Get(int index);

    // indexのアプリを起動する。
    // Pushで進むので、アプリ側からPop()すればランチャへ戻れる
    void Launch(int index);

    // 登録簿を空にする(主にテスト用。Setup()の冒頭でも呼ばれる)
    void Clear();

    // このOSに載せるアプリをまとめて登録する(実装は App_List.cpp)
    void Setup();
}
