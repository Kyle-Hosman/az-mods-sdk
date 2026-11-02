/* Actual core provider/client init integration. The existing core fixture
 * supplies loader/auth and typed native-receipt boundaries, not capabilities. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include SDK_CUE_PROVIDER_FIXTURE
#include <stdarg.h>
#include <stdlib.h>
static struct { int ok; } g_sdk_sq;
#include SDK_CUE_CAPABILITIES
static unsigned client_starts,client_logs,client_mode;
static void *(*client_worker)(void *);static void *client_arg;
static char client_lines[32][768];
static const az_library_v1 client_library={.abi=AZ_LIBRARY_ABI,.size=sizeof client_library,
    .data_capabilities=sdk_data_capabilities,.deck_snapshot=sd_deck_snapshot_v1};
static const az_library_v1 *client_service(uint32_t abi){assert(abi==AZ_LIBRARY_ABI);return &client_library;}
static int client_subscribe(const char *id,void (*fn)(int,int,void *),void *user){
    assert(!strcmp(id,"sdk-state-probe"));
    /* Test owner 'one' is the core fixture's loader/auth identity seam. */
    return sd_on_deck_event("one",fn,user);
}
static int client_thread(const char *id,const char *name,void *(*fn)(void *),void *user){
    assert(!strcmp(id,"sdk-state-probe")&&!strcmp(name,"sdk-public-state"));client_starts++;
    if(client_mode==2)return -1;client_worker=fn;client_arg=user;return 0;
}
static void client_log(const char *id,const char *format,...){
    assert(!strcmp(id,"sdk-state-probe")&&client_logs<32);va_list ap;va_start(ap,format);
    int n=vsnprintf(client_lines[client_logs],sizeof client_lines[client_logs],format,ap);va_end(ap);
    assert(n>=0&&(size_t)n<sizeof client_lines[client_logs]);client_logs++;
}
static int client_sleep(const struct timespec *delay,struct timespec *unused){
    (void)unused;assert(delay->tv_sec==1);
    if(client_mode==3)g_cm[0].state=0; /* Real dispatcher must refuse disabled owners. */
    for(int d=0;d<4;d++)next(d,100u+(unsigned)d,1);
    sdk_deck_tick(200);return 0;
}
#define nanosleep client_sleep
#define PROBE_CUE_ONLY 1
#define PROBE_MAX_TICKS 2u
#define PROBE_MAX_RECORDS 8u
#include PUBLIC_PROBE_IMPL
int main(int argc,char **argv){
    assert(argc==3);int expected_support=atoi(argv[1]);client_mode=(unsigned)atoi(argv[2]);
    sdk_deck_tick(100); /* Resolve the actual provider function slots as GUI collection does. */
    az_data_capabilities capabilities={.size=sizeof capabilities};
    assert(sdk_data_capabilities(&capabilities)==AZ_OK);
    assert(!capabilities.commands); /* No native command endpoint in this fixture. */
    assert(!!(capabilities.live_fields&AZ_LIVE_CUE_REVISION)==expected_support);
    for(int d=0;d<4;d++){
        az_deck_snapshot s={.size=sizeof s};assert(sd_deck_snapshot_v1(d,&s)==AZ_OK);
        assert(!(s.valid&AZ_LIVE_CUE_REVISION)&&!s.cue_unit_generation&&!s.cue_revision&&!s.cue_observed_ns);
    }
    puts("REAL_CUE_PROVIDER_EMPTY: all4 unknown, actual data_capabilities/snapshot ran");fflush(stdout);
    if(client_mode==1)g_cm_loading=1;
    az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof core,.library_service=client_service,
        .on_deck_event=client_subscribe,.thread_start=client_thread,.log=client_log};
    int rc=az_mod_v1_entry()->init(&core);
    if(client_mode==1){assert(rc==-1&&!client_starts&&!client_worker);return 0;}
    if(client_mode==2){assert(rc==-1&&client_starts==1&&!client_worker);g_cm[0].state=0;client_sleep(&(struct timespec){1,0},NULL);
        for(int d=0;d<4;d++)assert(!atomic_load(&probe_cue_events[d]));return 0;}
    assert(rc==0&&client_starts==1&&client_worker&&"client must register and wait before any cue receipt");
    client_worker(client_arg);
    unsigned rows=0;
    for(unsigned i=0;i<client_logs;i++)if(strstr(client_lines[i],"SDK_PUBLIC_CUE v=1")){
        rows++;assert(strstr(client_lines[i],rows<=4?"cue_valid=0":"cue_valid=1"));
        assert(!!strstr(client_lines[i],"event_ns=0")==!!(rows<=4||client_mode==3));
    }
    assert(rows==8);
    for(int d=0;d<4;d++){
        az_deck_snapshot s={.size=sizeof s};assert(sd_deck_snapshot_v1(d,&s)==AZ_OK);
        struct probe_cue_record v=probe_cue_values(d,probe_now(),AZ_OK,&s);
        assert(v.available&&v.unit==s.cue_unit_generation&&v.revision==s.cue_revision&&v.observed_ns==s.cue_observed_ns);
        assert(v.unit==100u+(unsigned)d&&v.revision==1&&!v.source_load);
    }
    puts("PASS real cue provider -> actual client empty init -> GUI callback/revision; lifecycle guards intact");return 0;
}
