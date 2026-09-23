// ホストテスト用のTLSクライアント。
//
// stubs/WiFi.h と同じく**PCビルドと同じ実装(OpenSSL)をそのまま使う**。
// このヘッダを取り込むテストは -lssl -lcrypto でリンクすること。
#pragma once

#include "WiFi.h"
#include "../../../pc/compat/WiFiClientSecure_PC.h"
