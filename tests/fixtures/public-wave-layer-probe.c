/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static const char *mode;
static unsigned calls, starts, logs, outstanding, releases, requested8, accepted, sleeps;
static unsigned request_busy, acquire_pending, release_busy;
static unsigned zero_queries, large_queries, outside_queries, unknown_queries;
static int in_worker;
static uint64_t now_ns = UINT64_C(1000000000), next_ticket = 40;
static char lines[64][768];
static void *(*worker_fn)(void *);
static void *worker_arg;
struct slot { uint64_t ticket; az_wave_query q; az_wave_snapshot view; uint8_t bytes[384]; };
static struct slot slots[2];
static az_track_ref reference = {sizeof(az_track_ref), AZ_DATA_VERSION, 101, 202, 303, 9, 0};
/* Fixed independent native-contract cases. The fake does not call a converter. */
static const struct { uint8_t raw[3], layered[6]; } golden[] = {
 {{255,128,1},{0x63,0x38,255,128,1,0}}, {{255,1,128},{0x53,0x38,255,128,1,0}},
 {{128,255,1},{0x62,0x38,255,128,1,0}}, {{1,255,128},{0x42,0x38,255,128,1,0}},
 {{128,1,255},{0x51,0x38,255,128,1,0}}, {{1,128,255},{0x41,0x38,255,128,1,0}},
 {{255,255,1},{0x86,0x20,255,1,0,0}}, {{255,128,128},{0x83,0x20,255,128,0,0}},
 {{0,127,0},{0x02,0x10,127,0,0,0}}, {{0,0,0},{0x08,0x10,0,0,0,0}},
 {{128,128,128},{0x08,0x10,128,0,0,0}}, {{254,0,254},{0x05,0x10,254,0,0,0}}
};
static int is(const char *s) { return !strcmp(mode,s); }
static void owner(const char *id) { assert(in_worker && !strcmp(id,"sdk-wave-layer-probe")); calls++; }
static int fake_clock(clockid_t c, struct timespec *t) {
 assert(c==CLOCK_MONOTONIC); now_ns+=1000;
 t->tv_sec=(time_t)(now_ns/UINT64_C(1000000000));t->tv_nsec=(long)(now_ns%UINT64_C(1000000000));
 return is("clock") || (is("clock_after_ticket") && outstanding) ? -1 : 0;
}
static int fake_sleep(const struct timespec *t, struct timespec *unused) {
 (void)unused;assert(t->tv_sec==0 && t->tv_nsec>0 && t->tv_nsec<=100000000L);
 sleeps++;now_ns+=(uint64_t)t->tv_nsec;return 0;
}
static int list_tracks(const char *id,int port,int playlist,int (*fn)(const az_track *,void *),void *u) {
 owner(id);assert(port==0 && playlist==0);
 if(is("no_track"))return 0;
 az_track t={.size=sizeof t,.id=9,.title="NEVER_COPY_OR_LOG"};assert(fn(&t,u)==1);return 0;
}
static int resolve(const char *id,int port,uint32_t track,az_track_ref *out) {
 owner(id);assert(port==0 && track==9 && out->size==sizeof *out);
 if(is("resolve_busy"))return AZ_BUSY;
 *out=reference;return AZ_OK;
}
static int request(const char *id,const az_track_ref *ref,const az_wave_query *q,uint64_t *ticket) {
 owner(id);assert(!memcmp(ref,&reference,sizeof *ref));assert(q->size==sizeof *q && q->version==AZ_DATA_VERSION && !q->reserved);
 if(is("error_ticket")){*ticket=999;return AZ_BUSY;}
 if(is("zero_ticket"))return AZ_OK;
 assert(!*ticket);if(is("request_busy") || (is("layer_request_busy") && q->encoding==8) || (is("busy_once") && !request_busy++))return AZ_BUSY;
 if(q->encoding==8)requested8++;
 if(q->encoding!=3 && q->encoding!=8){unknown_queries++;return AZ_UNSUPPORTED;}
 if(is("unsupported8") && q->encoding==8)return AZ_UNSUPPORTED;
 if(!q->column_count){zero_queries++;return AZ_INVALID;}
 if(q->column_count>4096){large_queries++;return AZ_LIMIT;}
 if(q->first_column>=1000){outside_queries++;if(is("immediate_range"))return AZ_INVALID;}
 struct slot *s=NULL;for(unsigned i=0;i<2;i++)if(!slots[i].ticket){s=&slots[i];break;}
 assert(s && outstanding<2);memset(s,0,sizeof *s);s->ticket=++next_ticket;s->q=*q;
 outstanding++;accepted++;*ticket=s->ticket;return AZ_OK;
}
static struct slot *find(uint64_t ticket) {
 for(unsigned i=0;i<2;i++)if(slots[i].ticket==ticket)return &slots[i];
 assert(!"wrong or released ticket");return NULL;
}
static int acquire(const char *id,uint64_t ticket,const az_wave_snapshot **out) {
 owner(id);struct slot *s=find(ticket);assert(!*out);
 if(is("pending") || (is("layer_pending") && s->q.encoding==8) || (is("pending_once") && !acquire_pending++))return AZ_PENDING;
 if(is("busy_once") && acquire_pending++==0)return AZ_BUSY;
 if(s->q.first_column>=1000)return AZ_INVALID;
 uint32_t count=1000-s->q.first_column;if(count>s->q.column_count)count=s->q.column_count;
 assert(count<=64);unsigned stride=s->q.encoding==3?3:6;
 for(uint32_t i=0;i<count;i++) {
  unsigned k=(s->q.first_column+i)%(sizeof golden/sizeof golden[0]);
  memcpy(s->bytes+i*stride,stride==3?golden[k].raw:golden[k].layered,stride);
 }
 s->view=(az_wave_snapshot){.size=sizeof s->view,.version=AZ_DATA_VERSION,.track=reference,
  .revision=77,.observed_ns=123,.info={AZ_AVAIL_READY,s->q.encoding==3?AZ_PROV_FILE:AZ_PROV_DERIVED,77,123},
  .encoding=s->q.encoding,.first_column=s->q.first_column,.column_count=count,.total_columns=1000,
  .bytes_per_column=stride,.data_bytes=count*stride,.start_s=s->q.first_column/150.0,
  .seconds_per_column=1.0/150.0,.columns=s->bytes};
 if(s->q.encoding==8) {
  if(is("reserved"))s->bytes[5]=0x7f;
  if(is("inverse"))s->bytes[2]^=1;
  if(is("provenance"))s->view.info.provenance=AZ_PROV_FILE;
  if(is("bounds"))s->view.first_column++;
  if(is("ref"))s->view.track.source_generation++;
  if(is("clock_axis"))s->view.seconds_per_column=1.0/44100.0;
  if(is("stride"))s->view.bytes_per_column=3;
  if(is("bytes"))s->view.data_bytes--;
  if(is("availability"))s->view.info.availability=AZ_AVAIL_LOADING;
  if(is("null_payload"))s->view.columns=NULL;
  if(is("count"))s->view.column_count++;
  if(is("header"))s->bytes[1]=0;
 }
 *out=&s->view;return AZ_OK;
}
static int release(const char *id,uint64_t ticket) {
 owner(id);struct slot *s=find(ticket);
 if(is("busy_once") && !release_busy++)return AZ_BUSY;
 s->ticket=0;assert(outstanding);outstanding--;releases++;return AZ_OK;
}
static az_library_v1 library={.abi=AZ_LIBRARY_ABI,.size=sizeof library,.lib_tracks=list_tracks,
 .track_resolve=resolve,.waveform_request=request,.waveform_acquire=acquire,.track_release=release};
static const az_library_v1 *service(uint32_t abi) { assert(in_worker && abi==AZ_LIBRARY_ABI);return is("missing_service")?NULL:&library; }
static void log_line(const char *id,const char *format,...) {
 assert(!strcmp(id,"sdk-wave-layer-probe") && logs<64);va_list a;va_start(a,format);
 int n=vsnprintf(lines[logs],sizeof lines[logs],format,a);va_end(a);assert(n>0 && (size_t)n<sizeof lines[logs]);
 assert(!strstr(lines[logs],"NEVER_COPY_OR_LOG"));puts(lines[logs++]);
}
static int start(const char *id,const char *name,void *(*fn)(void *),void *arg) {
 assert(!strcmp(id,"sdk-wave-layer-probe") && !strcmp(name,"sdk-public-wave") && fn);starts++;
 if(is("start_failure"))return -1;worker_fn=fn;worker_arg=arg;return 0;
}
#define clock_gettime fake_clock
#define nanosleep fake_sleep
#ifndef PUBLIC_WAVE_PROBE_IMPL
#define PUBLIC_WAVE_PROBE_IMPL "../../examples/wave-layer-probe/probe.c"
#endif
#include PUBLIC_WAVE_PROBE_IMPL
#undef clock_gettime
#undef nanosleep
int main(int argc,char **argv) {
 assert(argc==2);mode=argv[1];
 az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof core,.log=log_line,.thread_start=start,.library_service=service};
 const az_mod_v1 *d=az_mod_v1_entry();assert(d && !strcmp(d->id,"sdk-wave-layer-probe") && !d->tick && !d->on_frame);
 int rc=d->init(&core);assert(!calls && !outstanding && starts==1);
 if(is("start_failure")){assert(rc==-1 && !worker_fn);puts("PUBLIC_WAVE_FIXTURE_OK start_failure");return 0;}
 assert(!rc && worker_fn);in_worker=1;worker_fn(worker_arg);in_worker=0;
 printf("PUBLIC_WAVE_BASELINE_OK calls=%u accepted=%u released=%u\n",calls,accepted,releases);fflush(stdout);
 assert(outstanding==0 && accepted==releases && logs>0 && calls<512 && sleeps<256);
 const char *last=lines[logs-1];
 if(is("good") || is("pending_once") || is("busy_once") || is("immediate_range")) {
  assert(requested8 && "missing public encoding8 request after managed-worker forwarding baseline");
  assert(strstr(last,"status=PASS") && "missing complete encoding8 inverse/boundary acceptance");
  assert(accepted==(is("immediate_range")?5u:7u) && accepted==releases);
  assert(zero_queries==2 && large_queries==2 && outside_queries==2 && unknown_queries==1);
 } else if(is("pending") || is("layer_pending") || is("layer_request_busy") || is("request_busy") || is("resolve_busy") || is("no_track") || is("missing_service") || is("unsupported8") || is("clock") || is("clock_after_ticket")) {
  assert(strstr(last,"status=INCOMPLETE") && !strstr(last,"status=PASS"));
 } else assert(strstr(last,"status=FAIL") && !strstr(last,"status=PASS"));
 puts("PUBLIC_WAVE_FIXTURE_OK");return 0;
}
