/* SPDX-License-Identifier: GPL-3.0-only */
/* Host integration of the public probe with exact sealed core headers. The
 * catalog/ref bridge and worker schedule are fixtures; the queue, provider,
 * parser, derived payload, snapshots and ticket release are production code. */
#define _DARWIN_C_SOURCE 1
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char *we_mode;
static uint64_t we_ns = UINT64_C(1000000000);
static unsigned we_calls, we_pending, we_sleeps, we_loads, we_pumps;
static unsigned we_accepted, we_released, we_large, we_zero, we_outside, we_unknown;
static int we_in_probe, we_in_storage, we_last_load;
static char we_dir[80], we_path[160], we_lines[64][768];
static unsigned we_logs;
static void *(*we_worker)(void *);
static void *we_worker_arg;
static const az_track_ref we_ref = {sizeof(az_track_ref), AZ_DATA_VERSION, 101, 202, 303, 9, 0};
#define WE_CHECK(c, message) do { if (!(c)) { \
    fprintf(stderr, "PUBLIC_WAVE_ENGINE_ASSERT line=%d %s\n", __LINE__, message); \
    fflush(stdout); exit(86); } } while (0)
static int we_is(const char *s) { return !strcmp(we_mode, s); }
static int we_clock(clockid_t clock, struct timespec *t) {
    WE_CHECK(clock == CLOCK_MONOTONIC, "unexpected clock");
    we_ns += 1000;
    t->tv_sec = (time_t)(we_ns / UINT64_C(1000000000));
    t->tv_nsec = (long)(we_ns % UINT64_C(1000000000));
    return 0;
}
#define clock_gettime we_clock
#include "sdk_data_engine.h"
#undef clock_gettime
#include "sdk_wave_provider.h"
static struct st_engine we_engine = ST_ENGINE_INIT;
static const uint8_t we_raw[][3] = {
    {255,128,1}, {255,1,128}, {128,255,1}, {1,255,128}, {128,1,255}, {1,128,255},
    {255,255,1}, {255,128,128}, {0,127,0}, {0,0,0}, {128,128,128}, {254,0,254}
};
static void we_cleanup(void) {
    if (we_path[0]) (void)unlink(we_path);
    if (we_dir[0]) (void)rmdir(we_dir);
}
static void we_be32(uint8_t *p, uint32_t n) {
    p[0]=(uint8_t)(n>>24); p[1]=(uint8_t)(n>>16); p[2]=(uint8_t)(n>>8); p[3]=(uint8_t)n;
}
static void we_file(int malformed) {
    uint8_t file[12 + 24 + 1000 * 3] = {0};
    memcpy(file,"PMAI",4); we_be32(file+4,12); we_be32(file+8,sizeof file);
    memcpy(file+12,"PWV7",4); we_be32(file+16,24); we_be32(file+20,sizeof file-12);
    we_be32(file+24,3); we_be32(file+28,malformed ? 1001 : 1000); we_be32(file+32,0x00960000);
    for (unsigned i=0;i<1000;i++) memcpy(file+36+i*3,we_raw[i%12],3);
    int fd=open(we_path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
    WE_CHECK(fd>=0,"fixture file open");
    size_t off=0;
    while(off<sizeof file) {
        ssize_t n=write(fd,file+off,sizeof file-off);
        if(n<0 && errno==EINTR) continue;
        WE_CHECK(n>0,"fixture file write"); off+=(size_t)n;
    }
    WE_CHECK(!close(fd),"fixture file close");
}
static int we_load(const az_track_ref *ref, uint32_t mask, struct st_data *data, void *ctx) {
    (void)ctx;
    WE_CHECK(we_in_storage && mask==ST_DETAIL && st_ref_equal(ref,&we_ref),"actual worker load arguments");
    we_loads++;
    we_last_load=st_wave_read(we_path,data);
    return we_last_load;
}
static void we_pump(struct st_engine *e) {
    WE_CHECK(!we_in_storage,"unexpected nested storage worker");
    we_pumps++; we_in_storage=1; st_work(e,we_load,NULL,NULL); we_in_storage=0;
}
/* Inverse membership oracle, not a sorter or forward encoder. */
static int we_inverse(const uint8_t raw[3], const uint8_t record[6]) {
    unsigned h=record[0]+256u*record[1], n=h>>12, prev=0, level=256;
    uint8_t recovered[3]={0};
    if(n<1 || n>3 || record[5]) return 0;
    if(!(raw[0]|raw[1]|raw[2])) return h==0x1008 && !(record[2]|record[3]|record[4]);
    for(unsigned i=0;i<3;i++) {
        unsigned id=(h>>(4*i))&15, value=record[2+i], bits=0;
        if(i>=n) { if(id || value) return 0; continue; }
        switch(id) { case 1:bits=4;break;case 2:bits=2;break;case 3:bits=1;break;
        case 4:bits=6;break;case 5:bits=5;break;case 6:bits=3;break;case 8:bits=7;break;default:return 0; }
        if(!value || value>=level || bits==prev || (bits&prev)!=prev) return 0;
        for(unsigned channel=0;channel<3;channel++) if((bits&(1u<<channel)) && !recovered[channel]) recovered[channel]=(uint8_t)value;
        prev=bits; level=value;
    }
    return !memcmp(raw,recovered,3);
}
static void we_baseline(void) {
    struct st_engine e=ST_ENGINE_INIT;
    az_wave_query q={sizeof q,AZ_DATA_VERSION,AZ_WAVE_PWV7_BANDS,0,12,0};
    const az_wave_snapshot *v=NULL; uint64_t ticket=0;
    WE_CHECK(st_request_with_wave(&e,3,&we_ref,ST_DETAIL,&q,&ticket)==AZ_OK && ticket,"real queue acceptance");
    WE_CHECK(st_wave_acquire(&e,3,ticket,&v)==AZ_PENDING && !v,"real pre-worker pending");
    we_pump(&e);
    WE_CHECK(st_wave_acquire(&e,3,ticket,&v)==AZ_OK && v,"real worker raw publication");
    WE_CHECK(v->total_columns==1000 && v->column_count==12 && v->data_bytes==36 &&
             v->bytes_per_column==3 && v->info.provenance==AZ_PROV_FILE &&
             v->seconds_per_column==1.0/150.0 && !memcmp(v->columns,we_raw,sizeof we_raw),"real raw file/axis baseline");
    WE_CHECK(st_release(&e,3,ticket,0)==AZ_OK,"baseline raw release");
    ticket=0; v=NULL; q.encoding=AZ_WAVE_PWV7_LAYERED_RAW;
    WE_CHECK(st_request_with_wave(&e,3,&we_ref,ST_DETAIL,&q,&ticket)==AZ_OK && ticket,"real layered queue acceptance");
    we_pump(&e);
    WE_CHECK(st_wave_acquire(&e,3,ticket,&v)==AZ_OK && v && v->bytes_per_column==6 &&
             v->data_bytes==72 && v->info.provenance==AZ_PROV_DERIVED && v->seconds_per_column==1.0/150.0,
             "real layered publication/derived baseline");
    for(unsigned i=0;i<12;i++) WE_CHECK(we_inverse(we_raw[i],v->columns+i*6),"real layered reserved/inverse baseline");
    const uint8_t first[6]={0x63,0x38,255,128,1,0};
    WE_CHECK(!memcmp(v->columns,first,sizeof first),"independent native-schema first record");
    WE_CHECK(st_release(&e,3,ticket,0)==AZ_OK,"baseline layered release");
    q.column_count=4097; ticket=0;
    WE_CHECK(st_request_with_wave(&e,3,&we_ref,ST_DETAIL,&q,&ticket)==AZ_LIMIT && !ticket,"real4097 limit baseline");
    st_destroy(&e);
    puts("PUBLIC_WAVE_ENGINE_BASELINE_OK real_queue_pending=1 real_st_work=1 real_st_wave_read=1 raw=3 derived=8 reserved_inverse=1 limit4097=AZ_LIMIT");
    fflush(stdout);
    we_loads=we_pumps=0; we_last_load=0;
}
static void we_owner(const char *id) {
    WE_CHECK(we_in_probe && !we_in_storage && !strcmp(id,"sdk-wave-layer-probe"),"public owner bridge");
    we_calls++;
}
static int we_tracks(const char *id,int port,int playlist,int (*fn)(const az_track *,void *),void *u) {
    we_owner(id); WE_CHECK(port==0 && playlist==0,"explicit public enumeration");
    az_track t={.size=sizeof t,.id=9,.title="FIXTURE_TITLE_NOT_FOR_LOGS"};
    WE_CHECK(fn(&t,u)==1,"first public callback"); return 0;
}
static int we_resolve(const char *id,int port,uint32_t track,az_track_ref *out) {
    we_owner(id); WE_CHECK(port==0 && track==9 && out && out->size==sizeof *out,"reference bridge");
    *out=we_ref; return AZ_OK;
}
static int we_request(const char *id,const az_track_ref *ref,const az_wave_query *q,uint64_t *ticket) {
    we_owner(id); WE_CHECK(st_ref_equal(ref,&we_ref) && ticket && !*ticket,"public queue arguments");
    if(!q->column_count) we_zero++;
    if(q->column_count>4096) we_large++;
    if(q->first_column>=1000) we_outside++;
    if(q->encoding!=3 && q->encoding!=8) we_unknown++;
    int rc=st_request_with_wave(&we_engine,3,ref,ST_DETAIL,q,ticket);
    if(rc==AZ_OK) we_accepted++;
    return rc;
}
static int we_acquire(const char *id,uint64_t ticket,const az_wave_snapshot **out) {
    we_owner(id);
    int rc=st_wave_acquire(&we_engine,3,ticket,out);
    if(rc==AZ_PENDING) we_pending++;
    return rc;
}
static int we_release(const char *id,uint64_t ticket) {
    we_owner(id); int rc=st_release(&we_engine,3,ticket,0);
    if(rc==AZ_OK) we_released++;
    return rc;
}
static az_library_v1 we_library={.abi=AZ_LIBRARY_ABI,.size=sizeof we_library,.lib_tracks=we_tracks,
    .track_resolve=we_resolve,.waveform_request=we_request,.waveform_acquire=we_acquire,.track_release=we_release};
static const az_library_v1 *we_service(uint32_t abi) {
    WE_CHECK(we_in_probe && abi==AZ_LIBRARY_ABI,"public service lookup"); return &we_library;
}
static int we_sleep(const struct timespec *t,struct timespec *remaining) {
    (void)remaining;
    WE_CHECK(we_in_probe && t->tv_sec==0 && t->tv_nsec>0 && t->tv_nsec<=100000000L,"bounded actual requested sleep");
    we_sleeps++; we_ns+=(uint64_t)t->tv_nsec;
    if(!we_is("permanent") && (!we_is("delayed") || we_pending>=27)) we_pump(&we_engine);
    return 0;
}
static void we_log(const char *id,const char *format,...) {
    WE_CHECK(!strcmp(id,"sdk-wave-layer-probe") && we_logs<64,"bounded public log");
    va_list a; va_start(a,format); int n=vsnprintf(we_lines[we_logs],sizeof we_lines[0],format,a); va_end(a);
    WE_CHECK(n>0 && (size_t)n<sizeof we_lines[0] && !strstr(we_lines[we_logs],"FIXTURE_TITLE_NOT_FOR_LOGS"),"bounded scalar-only log");
    puts(we_lines[we_logs++]);
}
static int we_start(const char *id,const char *name,void *(*fn)(void *),void *arg) {
    WE_CHECK(!strcmp(id,"sdk-wave-layer-probe") && !strcmp(name,"sdk-public-wave") && fn && !we_worker,"managed thread bridge");
    we_worker=fn; we_worker_arg=arg; return 0;
}
#define clock_gettime we_clock
#define nanosleep we_sleep
#ifndef PUBLIC_WAVE_PROBE_IMPL
#define PUBLIC_WAVE_PROBE_IMPL "../../examples/wave-layer-probe/probe.c"
#endif
#include PUBLIC_WAVE_PROBE_IMPL
#undef clock_gettime
#undef nanosleep
int main(int argc,char **argv) {
    WE_CHECK(argc==2,"mode argument"); we_mode=argv[1];
    WE_CHECK(we_is("immediate") || we_is("delayed") || we_is("permanent") || we_is("malformed"),"known mode");
    strcpy(we_dir,"/tmp/az-public-wave-engine-XXXXXX");
    WE_CHECK(mkdtemp(we_dir)!=NULL,"private temporary directory");
    WE_CHECK(atexit(we_cleanup)==0,"cleanup registration");
    WE_CHECK(snprintf(we_path,sizeof we_path,"%s/ANLZ0000.2EX",we_dir)>0,"fixture path");
    we_file(0); we_baseline();
    if(we_is("malformed")) we_file(1);
    az_core_v1 c={.abi=AZ_MOD_ABI,.size=sizeof c,.log=we_log,.thread_start=we_start,.library_service=we_service};
    const az_mod_v1 *d=az_mod_v1_entry();
    WE_CHECK(d && !d->tick && !d->on_frame && !d->init(&c) && we_worker && !we_calls,"real public init bridge");
    we_in_probe=1; we_worker(we_worker_arg); we_in_probe=0;
    unsigned owned=0; for(unsigned i=0;i<ST_TICKETS;i++) owned+=we_engine.tickets[i].id!=0;
    printf("PUBLIC_WAVE_ENGINE_SERVICE_DONE mode=%s calls=%u accepted=%u released=%u outstanding=%u pending=%u sleeps=%u loads=%u pumps=%u large=%u simulated_ns=%llu\n",
           we_mode,we_calls,we_accepted,we_released,owned,we_pending,we_sleeps,we_loads,we_pumps,we_large,(unsigned long long)we_ns);
    fflush(stdout); st_destroy(&we_engine);
    WE_CHECK(!owned && we_accepted==we_released && we_logs && we_calls<=257 && we_sleeps<=256 && we_ns<UINT64_C(31000000000),"finite budgets and real accepted-ticket cleanup");
    const char *last=we_lines[we_logs-1];
    if(we_is("immediate") || we_is("delayed")) {
        if(we_is("immediate") && !strstr(last,"status=PASS")) {
            WE_CHECK(we_large==1 && strstr(last,"status=FAIL reason=refusal_status"),"unexpected immediate failure before intended limit assertion");
            WE_CHECK(0,"missing_probe_acceptance_of_real_AZ_LIMIT_4097");
        }
        if(we_is("delayed") && !strstr(last,"status=PASS")) {
            WE_CHECK(!we_loads && we_pending==24 && strstr(last,"status=INCOMPLETE reason=timeout"),"unexpected delayed failure before intended scheduling assertion");
            WE_CHECK(0,"missing_probe_wait_for_actual_worker_after_24_pending");
        }
        WE_CHECK(strstr(last,"status=PASS") && we_large==2 && we_zero==2 && we_outside==2 && we_unknown==1 && we_accepted==7 && we_loads==7,"complete actual-engine public acceptance");
        if(we_is("delayed")) WE_CHECK(we_pending>=27,"real delayed pump threshold");
    } else {
        WE_CHECK(strstr(last,"status=INCOMPLETE") && !strstr(last,"status=PASS"),"non-ready provider cannot pass");
        if(we_is("permanent")) WE_CHECK(!we_loads && we_pending && we_accepted==1,"permanent actual queue remains unworked");
        else WE_CHECK(we_loads==1 && we_last_load==AZ_FAILED && we_accepted==1,"real malformed PWV7 refusal");
    }
    puts("PUBLIC_WAVE_ENGINE_FIXTURE_OK"); return 0;
}
