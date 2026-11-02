/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SLIP_PROBE_BINDING_H
#define SLIP_PROBE_BINDING_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "native/common/azsha256.h"
struct slip_code { uintptr_t rva; size_t bytes; const char *sha256; };
struct slip_profile {
    const char *name, *fw_tag;
    struct slip_code callback, dispatcher;
    uintptr_t caller;
};
struct slip_rx { uintptr_t begin, end; };
static int slip_code_matches(const struct slip_code *code, uintptr_t base,
                             const struct slip_rx *rx, size_t count) {
    if (!code || !code->bytes || !code->sha256 || base > UINTPTR_MAX-code->rva) return 0;
    uintptr_t address=base+code->rva;
    if (address>UINTPTR_MAX-code->bytes) return 0;
    for(size_t i=0;i<count;i++) {
        if(address>=rx[i].begin && address+code->bytes<=rx[i].end) {
            char digest[65];
            azsha256_hex((const void *)address,code->bytes,digest);
            return !strcmp(digest,code->sha256);
        }
    }
    return 0;
}
static int slip_profile_matches(const struct slip_profile *p,const char *tag,
                                uintptr_t base,const struct slip_rx *rx,size_t count) {
    return p && tag && !strcmp(p->fw_tag,tag) && p->caller>p->dispatcher.rva &&
        p->caller-p->dispatcher.rva<p->dispatcher.bytes &&
        slip_code_matches(&p->callback,base,rx,count) &&
        slip_code_matches(&p->dispatcher,base,rx,count);
}
#endif
