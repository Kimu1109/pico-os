// ホストテスト(script/host_test/run.sh)専用のダミーヘッダ。実機ビルドでは使われない。
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cstddef>
static inline unsigned long millis(){ return 0; }
#define LED_BUILTIN 0
