/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../examples/slip-callback-probe/binding.h"
int main(void) {
    unsigned char bytes[128];
    for(size_t i=0;i<sizeof bytes;i++)bytes[i]=(unsigned char)(i*7+3);
    char a[65],b[65];azsha256_hex(bytes+16,32,a);azsha256_hex(bytes+64,48,b);
    struct slip_profile p={"test","ep147-0123456789abcdef",{16,32,a},{64,48,b},88};
    uintptr_t base=(uintptr_t)bytes;
    struct slip_rx rx={base,base+sizeof bytes};
    assert(slip_profile_matches(&p,p.fw_tag,base,&rx,1));
    assert(!slip_profile_matches(&p,"ep147-1123456789abcdef",base,&rx,1));
    bytes[20]^=1;
    assert(!slip_profile_matches(&p,p.fw_tag,base,&rx,1));
    bytes[20]^=1;bytes[80]^=1;
    assert(!slip_profile_matches(&p,p.fw_tag,base,&rx,1));
    bytes[80]^=1;p.callback.rva++;
    assert(!slip_profile_matches(&p,p.fw_tag,base,&rx,1));
    p.callback.rva--;rx.end=base+100;
    assert(!slip_profile_matches(&p,p.fw_tag,base,&rx,1));
    rx.end=base+sizeof bytes;p.caller=128;
    assert(!slip_profile_matches(&p,p.fw_tag,base,&rx,1));
    assert(!slip_profile_matches(&p,p.fw_tag,UINTPTR_MAX-8,&rx,1));
    puts("Slip signature/range/tag/caller guards PASS");return 0;
}
