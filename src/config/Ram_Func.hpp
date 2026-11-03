#pragma once

// 毎フレーム何度も通る関数を、フラッシュ(XIP)ではなくRAMに置く印。docs/design/performance.md「よく通る所をRAMに置く」。
//
//   PICO_RAM_FUNC const uint8_t* U8g2GlyphIndex::find(uint16_t encoding) { ... }
//
// 実機のコードはフラッシュからXIPのキャッシュ(16KB)を通して読む。フォントの字のデータ(277KB)のように
// フラッシュの大きなデータを読みながら回る所は、そのデータの読みでコードがキャッシュから追い出され、
// 次に通るときにまたフラッシュから読む。RAMに置いたコードはキャッシュを使わない。
// `.time_critical.*` の節は arduino-pico のリンカスクリプト(memmap_default.ld)が .data(RAM)へ集める
// (pico-sdk の __not_in_flash_func と同じ仕組み)。RAMはそのぶん減る(静的なRAMとして数えられる)。
//
// **付けるのは「大きなフラッシュのデータを読みながら何度も通る」小さな関数だけ**。RAMは1バイトでも惜しいので、
// 1フレームに数回しか通らない関数や、RAMだけを読む短いループ(キャッシュに乗ったままになる)には付けない。
//
// -DPICO_HOT_IN_RAM=0 で全部フラッシュへ戻す(実機での前後の比べ用)。PC/Web/ホストテストでは何もしない。

#if !defined(PICO_HOT_IN_RAM)
#define PICO_HOT_IN_RAM 1
#endif

#if defined(PICOOS_PC) || !PICO_HOT_IN_RAM
#define PICO_RAM_FUNC
#else
#define PICO_RAM_FUNC __attribute__((section(".time_critical.picoos")))
#endif
