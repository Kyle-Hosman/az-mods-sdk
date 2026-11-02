/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "azmod-mod.h"
#include "capture.h"
#include "profiles.h"
#include <link.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#define ID "sdk-slip-capture"
#define SLIP_MAX_TICKS 18000u
extern void azsdk_cache_entry_0(void) __attribute__((visibility("hidden")));
static const az_core_v1 *slip_core;
static _Atomic int slip_installed;
struct slip_image { uintptr_t base;struct slip_rx rx[16];size_t count;int found; };
static int slip_main_image(struct dl_phdr_info *info,size_t size,void *user) {
    (void)size;
    struct slip_image *image=user;
    if(info->dlpi_name && info->dlpi_name[0])return 0;
    image->base=info->dlpi_addr;
    for(unsigned i=0;i<info->dlpi_phnum;i++) {
        const ElfW(Phdr) *p=&info->dlpi_phdr[i];
        if(p->p_type!=PT_LOAD || !(p->p_flags&PF_X))continue;
        if(image->count==16 || image->base>UINTPTR_MAX-p->p_vaddr)return 1;
        uintptr_t begin=image->base+p->p_vaddr;
        if(begin>UINTPTR_MAX-p->p_memsz)return 1;
        image->rx[image->count++]=(struct slip_rx){begin,begin+p->p_memsz};
    }
    image->found=1;return 1;
}
static void slip_stats(uint64_t emitted,uint64_t missed,const char *reason) {
    struct slip_capture_stats s;slip_capture_get_stats(&s);
    slip_core->log(ID,"SDK_SLIP_CAPTURE_STATS v=1 saved=%llu emitted=%llu dropped=%llu rejected=%llu missed=%llu reason=%s",
        (unsigned long long)s.saved,(unsigned long long)emitted,(unsigned long long)s.dropped,
        (unsigned long long)s.rejected,(unsigned long long)missed,reason);
}
static void *slip_worker(void *unused) {
    (void)unused;uint64_t next=1,emitted=0,missed=0;
    for(unsigned tick=0;tick<SLIP_MAX_TICKS;tick++) {
        int state=atomic_load_explicit(&slip_installed,memory_order_acquire);
        if(state<0){slip_stats(emitted,missed,"install_failed");return NULL;}
        if(state>0) {
            struct slip_capture_stats stats;slip_capture_get_stats(&stats);
            uint64_t first=stats.saved>128?stats.saved-127:1;
            if(next<first){missed+=first-next;next=first;}
            while(next<=stats.saved) {
                struct slip_capture_record record;
                if(!slip_capture_read(next,&record))break;
                slip_core->log(ID,"SDK_SLIP_CAPTURE v=1 seq=%llu observed_ns=%llu raw=%u receiver=0x%llx caller=0x%llx",
                    (unsigned long long)record.sequence,(unsigned long long)record.observed_ns,record.raw,
                    (unsigned long long)record.receiver,(unsigned long long)record.caller);
                emitted++;next++;
            }
            if(tick%100u==0)slip_stats(emitted,missed,"running");
            if(stats.saved==512 && next>stats.saved){slip_stats(emitted,missed,"record_limit");return NULL;}
        }
        const struct timespec delay={0,100000000};
        if(nanosleep(&delay,NULL) && errno!=EINTR){slip_stats(emitted,missed,"sleep");return NULL;}
    }
    slip_stats(emitted,missed,"time_limit");return NULL;
}
static int init(const az_core_v1 *core) {
    if(!core || core->abi!=AZ_MOD_ABI || !AZ_HAS(core,claim_hook) || !AZ_HAS(core,fw_tag) ||
       !AZ_HAS(core,log) || !AZ_HAS(core,thread_start))return -1;
    const struct slip_profile *profile=NULL;
    const char *tag=core->fw_tag();
    struct slip_image image={0};dl_iterate_phdr(slip_main_image,&image);
    if(!image.found)return -1;
    for(size_t i=0;i<sizeof slip_profiles/sizeof *slip_profiles;i++)
        if(slip_profile_matches(&slip_profiles[i],tag,image.base,image.rx,image.count)){profile=&slip_profiles[i];break;}
    if(!profile)return -1;
    if(!slip_capture_arm(image.base+profile->caller))return -1;
    slip_core=core;
    if(core->thread_start(ID,"sdk-slip-capture",slip_worker,NULL))return -1;
    /* Last fallible action. Failed init never unloads this module or its worker. */
    if(core->claim_hook(ID,image.base+profile->callback.rva,(void *)azsdk_cache_entry_0,&azsdk_cache_entry_originals[0])) {
        atomic_store_explicit(&slip_installed,-1,memory_order_release);return -1;
    }
    atomic_store_explicit(&slip_installed,1,memory_order_release);
    core->log(ID,"SDK_SLIP_CAPTURE_READY v=1 profile=%s target=0x%llx caller=0x%llx max_records=512 ring=128 semantics=raw_no_deck",
        profile->name,(unsigned long long)(image.base+profile->callback.rva),(unsigned long long)(image.base+profile->caller));
    return 0;
}
static const char *const tags[]={SLIP_PROFILE_TAGS,NULL};
static const az_mod_v1 mod={.abi=AZ_MOD_ABI,.size=sizeof mod,.id=ID,.fw_tags=tags,.init=init};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void){return &mod;}
