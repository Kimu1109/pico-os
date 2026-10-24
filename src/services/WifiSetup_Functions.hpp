#pragma once

#include <cstdint>

#include "util/FixedString.hpp"
#include "consts.hpp"

// AP設定モード: 本体がWi-Fiのアクセスポイントになり、スマホから繋ぎ先のWi-Fiを送ってもらう。
// 中身(ページ・DNSの答え)は net/Setup_Portal、APの出し入れは NetworkFunctions::StartSetupAp()。
//
//   Start() … APを始める(名前は「<本体の名前>-setup」、パスワードは毎回ちがう8桁の数字)。
//             APに繋いだスマホからの名前の問い合わせ(DNS)には全部本体のアドレスを答えるので、
//             スマホは「ログインが必要なネットワーク」としてページを自動で開く(開かなければ PageUrl())
//   Update() … 画面(WifiSetupScene)が開いている間、毎フレーム。DNSとHTTPを1つずつさばく
//   ページでSSID/パスワードが送られると WifiProfiles へ保存して State::Saved になる。
//   Stop()  … APを止め、Savedなら送られたネットワークへ、そうでなければ保存済みへの自動接続へ戻る
//
// 同時に相手をするHTTPの接続は1本だけ(スマホ1台で使う想定)。サーバの持ち物(要求と応答の溜め場所
// 約4.5KB)は Start() で new し Stop() で返す。
// PCビルドでは本物のAPは作れないので、ページだけ http://127.0.0.1:8081/ で開ける(PICOOS_SETUP_PORT で変更)。
// Webビルドでは使えない。
namespace WifiSetupFunctions {
    constexpr uint16_t kHttpPort = 80;
    constexpr uint16_t kDnsPort = 53;
    // 1本の接続で要求が揃うのを待つ上限(スマホが繋いだまま何も送らないとき)
    constexpr uint32_t kRequestTimeoutMs = 5000;

    enum class State : uint8_t {
        Idle,     // 止まっている
        Running,  // APを出してページを待っている
        Saved,    // ページからネットワークが送られ、保存した(SavedSsid())
    };

    bool Start(FixedString<PICO_STR_LL>& err);
    void Update();
    // APを止める。Savedなら送られたネットワークへ繋ぎ、そうでなければ保存済みへの自動接続に任せる
    void Stop();

    State GetState();
    const char* ApSsid();
    const char* ApPassword();
    // スマホで開くURL("http://192.168.42.1/")
    const char* PageUrl();
    const char* SavedSsid();
    int Clients();
}
