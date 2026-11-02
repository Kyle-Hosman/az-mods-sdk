/* Public client contract only; this fixture supplies no native cue evidence. */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned mode,registered,starts,snapshots,sleeps,logs;
static uint64_t now_ns=1000000000,changed_ns;
static void (*callback)(int,int,void *);static void *callback_user;
static void *(*worker)(void *);static void *worker_arg;
static char lines[32][768];
static int fake_clock(clockid_t id,struct timespec *t) {
    assert(id==CLOCK_MONOTONIC);now_ns+=1000;t->tv_sec=now_ns/1000000000;t->tv_nsec=now_ns%1000000000;return 0;
}
static int fake_sleep(const struct timespec *t,struct timespec *unused) {
    (void)unused;assert(t->tv_sec==1);sleeps++;now_ns+=1000000000;changed_ns=now_ns;
    for(int d=0;d<4;d++)callback(d,mode==1?AZ_DECK_LOADED:AZ_DECK_CUES_CHANGED,callback_user);
    callback(-1,AZ_DECK_CUES_CHANGED,callback_user);callback(4,AZ_DECK_CUES_CHANGED,callback_user);
    return 0;
}
static int snapshot(int d,az_deck_snapshot *s) {
    assert(d>=0&&d<4&&s->size==sizeof *s);snapshots++;
    s->version=AZ_DATA_VERSION;s->deck=(unsigned)d;s->valid=mode==2?0:AZ_LIVE_CUE_REVISION;
    s->cue_unit_generation=mode==6?0:100u+(unsigned)d;s->cue_revision=mode==7?0:changed_ns?2:1;
    s->cue_observed_ns=mode==8?0:mode==5?now_ns+1000000000:changed_ns?changed_ns:1;
    if(mode==3)s->deck=(unsigned)(d+1)%4;
    if(mode==4)s->size=offsetof(az_deck_snapshot,cue_observed_ns);
    if(mode==13&&!changed_ns){s->valid=0;s->cue_unit_generation=s->cue_revision=s->cue_observed_ns=0;}
    return mode==9?AZ_BUSY:AZ_OK;
}
static int caps(az_data_capabilities *c) {c->version=AZ_DATA_VERSION;c->live_fields=mode==13?0:AZ_LIVE_CUE_REVISION;c->commands=0;return 0;}
static int subscribe(const char *id,void (*fn)(int,int,void *),void *user) {
    assert(!strcmp(id,"sdk-state-probe")&&fn);registered++;callback=fn;callback_user=user;return mode==11?-1:0;
}
static az_library_v1 library={.abi=AZ_LIBRARY_ABI,.size=sizeof library,.data_capabilities=caps,.deck_snapshot=snapshot};
static const az_library_v1 *service(uint32_t abi) {assert(abi==AZ_LIBRARY_ABI);return &library;}
static int start(const char *id,const char *name,void *(*fn)(void *),void *user) {
    assert(!strcmp(id,"sdk-state-probe")&&!strcmp(name,"sdk-public-state"));starts++;
    if(mode==12)return -1;worker=fn;worker_arg=user;return 0;
}
static void log_line(const char *id,const char *format,...) {
    assert(!strcmp(id,"sdk-state-probe")&&logs<32);va_list ap;va_start(ap,format);
    int n=vsnprintf(lines[logs],sizeof lines[logs],format,ap);va_end(ap);assert(n>0&&(size_t)n<sizeof lines[logs]);logs++;
}
#define clock_gettime fake_clock
#define nanosleep fake_sleep
#define PROBE_CUE_ONLY 1
#define PROBE_MAX_TICKS 2u
#ifndef PROBE_MAX_RECORDS
#define PROBE_MAX_RECORDS 12u
#endif
#include "../../examples/public-state-probe/probe.c"
int main(int argc,char **argv) {
    mode=argc>1?(unsigned)atoi(argv[1]):0;
    az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof core,.log=log_line,.library_service=service,.thread_start=start,.on_deck_event=subscribe};
    if(mode==10)core.on_deck_event=NULL;
    int rc=az_mod_v1_entry()->init(&core);
    if(mode>=10&&mode<=12){assert(rc==-1&&!worker);assert(starts==(mode==12));return 0;}
    assert(rc==0&&registered==1&&starts==1&&worker);worker(worker_arg);
    assert(snapshots<=8&&sleeps<=1);
    unsigned cue_rows=0;
    for(unsigned n=0;n<logs;n++)if(strstr(lines[n],"SDK_PUBLIC_CUE v=1")) {
        cue_rows++;assert(strstr(lines[n],"source_load=0"));
        assert(strstr(lines[n],(mode>=2&&mode<=9)||(mode==13&&cue_rows<=4)?"cue_valid=0":"cue_valid=1"));
        if(cue_rows<=4||mode==1)assert(strstr(lines[n],"event_ns=0"));
        else assert(!strstr(lines[n],"event_ns=0"));
    }
    assert(cue_rows==(PROBE_MAX_RECORDS<8?PROBE_MAX_RECORDS:8));
    assert(strstr(lines[logs-1],"SDK_PUBLIC_CUE complete"));
    return 0;
}
