/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "fm_patch.h"
#include "fm_factory.inc"

/* Expand once during startup, before the audio owners are running. MIDI
 * program changes then read SRAM without decompression or heap allocation. */
static uint8_t factory_raw[FM1_FM_PROGRAMS*128];
static unsigned factory_ready;
/* Canonical Huffman over the LZ token stream (see the generator). */
static unsigned factory_bit;
static FM1_DSP_CODE unsigned factory_symbol(void)
{
    int code=0,first=0,index=0;
    for (unsigned len=1;len<16;++len) {
        code|=(factory_bits[factory_bit>>3]>>(7U-(factory_bit&7U)))&1U; ++factory_bit;
        int count=factory_counts[len];
        if (code-count<first) return factory_symbols[index+code-first];
        index+=count; first+=count; first<<=1; code<<=1;
    }
    return 0;
}
static FM1_DSP_CODE void factory_init(void)
{
    if (factory_ready) return;
    uint8_t *out=factory_raw;
    while (out<factory_raw+sizeof factory_raw) {
        unsigned token=factory_symbol();
        if (token<128) *out++=(uint8_t)token;
        else {
            /* 128..191: base-128 distance; 192..255: k+1 whole voices back. */
            unsigned low=factory_symbol(),distance=low&127U;
            if (token>=192U) distance=(low+1U)<<7;
            else if (low&128U) distance|=factory_symbol()<<7;
            for (unsigned n=(token&63U)+3U;n;--n) { *out=out[-(int)distance]; ++out; }
        }
    }
    factory_ready=1;
}
FM1_DSP_CODE const uint8_t *groove_fm_factory_raw(unsigned program)
{
    factory_init();
    return factory_raw+(program%FM1_FM_PROGRAMS)*128;
}
FM1_DSP_CODE void groove_fm_factory(uint8_t p[156], unsigned program)
{
    groove_fm_unpack(p,groove_fm_factory_raw(program));
}
