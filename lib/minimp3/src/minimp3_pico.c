/* pico-os: minimp3 の実装をここで1回だけ作る(README-pico-os.md)。
   - MINIMP3_ONLY_MP3: Layer I/II(MP1/MP2)は読まない(フラッシュを減らす)
   - MINIMP3_SCRATCH_PTR: 作業場所をスタックではなく minimp3_pico_scratch の指す場所に置く(pico-os の改造) */
#include "minimp3_pico.h"

void* minimp3_pico_scratch = 0;

#define MINIMP3_ONLY_MP3
#define MINIMP3_SCRATCH_PTR minimp3_pico_scratch
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

const size_t minimp3_pico_scratch_bytes = sizeof(mp3dec_scratch_t);
