/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after the ordinary core disk-provider fixture's pre-main helpers.
 * USB/SQLite/key and mod identity are fixtures. The real public probe, metadata
 * query, disk provider, queue and pthread worker run unchanged. */
static pthread_t full_thread;
static void *(*full_native_fn)(void *);
static void *full_native_arg;
static int full_stop, full_counting, full_opens, full_queries, full_keys, full_closes;
static unsigned full_accepted, full_released;
static int full_delay_ms, full_pass, full_outstanding = -1;
static __typeof__(g_sdk_sq.open_v2) full_open;
static __typeof__(g_sdk_sq.prepare) full_prepare;
static __typeof__(g_sdk_sq.key) full_key;
static __typeof__(g_sdk_sq.close) full_close;
static void *(*full_probe_fn)(void *);
static void *full_probe_arg;
#define FULL_CHECK(x) do { if (!(x)) { fprintf(stderr,"FULL_PROVIDER_ASSERT line=%d %s\n",__LINE__,#x); exit(86); } } while (0)
static int full_pause(const struct timespec *t, struct timespec *left) {
    if (__atomic_load_n(&full_stop,__ATOMIC_ACQUIRE)) pthread_exit(NULL);
    return nanosleep(t,left);
}
static void *full_native_entry(void *unused) { (void)unused; return full_native_fn(full_native_arg); }
static int full_create(pthread_t *out,const pthread_attr_t *given,void *(*fn)(void *),void *arg) {
    int policy=0,inherit=0; struct sched_param sp={0};
    FULL_CHECK(!pthread_attr_getschedpolicy(given,&policy) && policy==SCHED_OTHER);
    FULL_CHECK(!pthread_attr_getinheritsched(given,&inherit) && inherit==PTHREAD_EXPLICIT_SCHED);
    FULL_CHECK(!pthread_attr_getschedparam(given,&sp) && sp.sched_priority==0);
    pthread_attr_t attr; FULL_CHECK(!pthread_attr_init(&attr));
    FULL_CHECK(!pthread_attr_setinheritsched(&attr,inherit) && !pthread_attr_setschedpolicy(&attr,policy));
    FULL_CHECK(!pthread_attr_setschedparam(&attr,&sp));
    full_native_fn=fn;full_native_arg=arg;
    int rc=pthread_create(&full_thread,&attr,full_native_entry,NULL);*out=full_thread;
    FULL_CHECK(!pthread_attr_destroy(&attr));return rc;
}
static int full_count_open(const char *path,void **db,int flags,const char *vfs) {
    if (__atomic_load_n(&full_counting,__ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&full_opens,1,__ATOMIC_RELAXED);
        if(full_delay_ms) { struct timespec t={full_delay_ms/1000,(full_delay_ms%1000)*1000000L};nanosleep(&t,NULL); }
    }
    return full_open(path,db,flags,vfs);
}
static int full_count_prepare(void *db,const char *sql,int n,void **stmt,const char **tail) {
    if(__atomic_load_n(&full_counting,__ATOMIC_ACQUIRE)) __atomic_add_fetch(&full_queries,1,__ATOMIC_RELAXED);
    return full_prepare(db,sql,n,stmt,tail);
}
static int full_count_key(void *db,const void *key,int n) {
    if(__atomic_load_n(&full_counting,__ATOMIC_ACQUIRE)) __atomic_add_fetch(&full_keys,1,__ATOMIC_RELAXED);
    return full_key(db,key,n);
}
static int full_count_close(void *db) {
    if(__atomic_load_n(&full_counting,__ATOMIC_ACQUIRE)) __atomic_add_fetch(&full_closes,1,__ATOMIC_RELAXED);
    return full_close(db);
}
static void full_owner(const char *id) { FULL_CHECK(id && !strcmp(id,"sdk-wave-layer-probe")); }
static int full_tracks(const char *id,int port,int list,int (*cb)(const az_track *,void *),void *arg) {
    full_owner(id);return sdk_lib_tracks("mod-a",port,list,cb,arg);
}
static int full_resolve(const char *id,int port,uint32_t track,az_track_ref *ref) {
    full_owner(id);int rc=sdk_track_resolve("mod-a",port,track,ref);
    __atomic_store_n(&full_counting,1,__ATOMIC_RELEASE);return rc;
}
static int full_request(const char *id,const az_track_ref *ref,const az_wave_query *q,uint64_t *ticket) {
    full_owner(id);int rc=sdk_waveform_request("mod-a",ref,q,ticket);if(rc==AZ_OK)full_accepted++;return rc;
}
static int full_acquire(const char *id,uint64_t ticket,const az_wave_snapshot **out) { full_owner(id);return sdk_waveform_acquire("mod-a",ticket,out); }
static int full_release(const char *id,uint64_t ticket) { full_owner(id);int rc=sdk_track_release("mod-a",ticket);if(rc==AZ_OK)full_released++;return rc; }
static az_library_v1 full_library={.abi=AZ_LIBRARY_ABI,.size=sizeof full_library,.lib_tracks=full_tracks,.track_resolve=full_resolve,.waveform_request=full_request,.waveform_acquire=full_acquire,.track_release=full_release};
static const az_library_v1 *full_service(uint32_t abi) { FULL_CHECK(abi==AZ_LIBRARY_ABI);return &full_library; }
static int full_start(const char *id,const char *name,void *(*fn)(void *),void *arg) { full_owner(id);FULL_CHECK(!strcmp(name,"sdk-public-wave") && !full_probe_fn);full_probe_fn=fn;full_probe_arg=arg;return 0; }
static void full_log(const char *id,const char *fmt,...) {
    full_owner(id);char line[768];va_list ap;va_start(ap,fmt);int n=vsnprintf(line,sizeof line,fmt,ap);va_end(ap);FULL_CHECK(n>0 && (size_t)n<sizeof line);puts(line);
    if(strstr(line,"event=final ")) { full_pass=strstr(line," status=PASS ")!=NULL;const char *p=strstr(line,"outstanding=");if(p)full_outstanding=atoi(p+12); }
}
#include PUBLIC_WAVE_PROBE_IMPL
static void full_put32(unsigned char *p,unsigned n) { p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n; }
int main(int argc,char **argv) {
    FULL_CHECK(argc==2);full_delay_ms=atoi(argv[1]);FULL_CHECK(full_delay_ms>=0 && full_delay_ms<=3000);
    char root[]="/tmp/sdk-full-wave.XXXXXX";FULL_CHECK(mkdtemp(root));
    snprintf(g_media,sizeof g_media,"%s/media",root);snprintf(g_sysblock,sizeof g_sysblock,"%s/sys",root);snprintf(g_mounts,sizeof g_mounts,"%s/mounts",root);snprintf(st_test_mountinfo,sizeof st_test_mountinfo,"%s/mountinfo",root);
    FULL_CHECK(!mkdir(g_media,0700) && !mkdir(g_sysblock,0700));
    make_stick("sdb1","A");plug("sdb1","1-1","sdb1");mountinfo(root,100);
    char path[PATH_MAX];snprintf(path,sizeof path,"%s/sdb1/PIONEER/rekordbox/exportLibrary.db",g_media);
    sqlite3 *db=NULL;FULL_CHECK(!sqlite3_open(path,&db));sql(db,"ALTER TABLE content ADD COLUMN analysisDataFilePath TEXT");sql(db,"UPDATE content SET analysisDataFilePath='/full.DAT'");FULL_CHECK(!sqlite3_close(db));
    unsigned char file[36+1000*3]={0};memcpy(file,"PMAI",4);full_put32(file+4,12);full_put32(file+8,sizeof file);memcpy(file+12,"PWV7",4);full_put32(file+16,24);full_put32(file+20,sizeof file-12);full_put32(file+24,3);full_put32(file+28,1000);full_put32(file+32,0x00960000);
    for(unsigned i=0;i<1000;i++){file[36+3*i]=200;file[37+3*i]=100;file[38+3*i]=1;}
    snprintf(path,sizeof path,"%s/sdb1/full.2EX",g_media);FILE *f=fopen(path,"wb");FULL_CHECK(f && fwrite(file,1,sizeof file,f)==sizeof file && !fclose(f));
    g_st_enabled=1;g_st_started=0;FULL_CHECK(sdk_sq_resolve());
    full_open=g_sdk_sq.open_v2;full_prepare=g_sdk_sq.prepare;full_key=g_sdk_sq.key;full_close=g_sdk_sq.close;
    g_sdk_sq.open_v2=full_count_open;g_sdk_sq.prepare=full_count_prepare;g_sdk_sq.key=full_count_key;g_sdk_sq.close=full_count_close;
    az_core_v1 core={.abi=AZ_MOD_ABI,.size=sizeof core,.log=full_log,.library_service=full_service,.thread_start=full_start};
    FULL_CHECK(az_mod_v1_entry()->init(&core)==0 && full_probe_fn);full_probe_fn(full_probe_arg);
    FULL_CHECK(full_native_fn && g_st_started);__atomic_store_n(&full_stop,1,__ATOMIC_RELEASE);FULL_CHECK(!pthread_join(full_thread,NULL));
    printf("FULL_PROVIDER_RESULT pass=%d opens=%d keys=%d queries=%d closes=%d accepted=%u released=%u outstanding=%d\n",full_pass,full_opens,full_keys,full_queries,full_closes,full_accepted,full_released,full_outstanding);
    FULL_CHECK(full_outstanding==0 && full_accepted==full_released);
    st_destroy(&g_st);char command[PATH_MAX+20];snprintf(command,sizeof command,"rm -rf '%s'",root);FULL_CHECK(!system(command));
    FULL_CHECK(full_pass && full_opens==1 && full_keys==1 && full_queries==1 && full_closes==1);
    puts("FULL_PROVIDER_OK");return 0;
}
