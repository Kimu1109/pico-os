/* pico-os: minimp3 の実装の作り方と、作業場所の受け渡し(README-pico-os.md) */
#ifndef MINIMP3_PICO_H
#define MINIMP3_PICO_H
#include <stddef.h>
#include "minimp3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* mp3dec_decode_frame() が使う作業場所(mp3dec_scratch_t)の大きさ。呼ぶ前に
   minimp3_pico_scratch へこの大きさ以上・4バイト境界の場所を入れておくこと(スタックへ約16KBを置かないため) */
extern const size_t minimp3_pico_scratch_bytes;
extern void* minimp3_pico_scratch;

#ifdef __cplusplus
}
#endif

#endif /* MINIMP3_PICO_H */
