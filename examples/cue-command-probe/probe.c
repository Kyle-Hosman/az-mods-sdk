/* SPDX-License-Identifier: GPL-3.0-only */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "azmod-mod.h"
#include "azmod-timestamps.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define ID "sdk-cue-command-probe"
#define LIMIT UINT64_C(16777216)
#define CUE_IDLE_NS UINT64_C(1800000000000)
#define CUE_LIFETIME_NS UINT64_C(7200000000000)
#define CUE_DRAIN_NS UINT64_C(30000000000)
struct cue_input { uint64_t pid,id,load,revision,timeout,submitted_ns,deadline; uint32_t deck,op,flags,classification,entry,rgb; double position,end,beats; char action[9],comment[128]; };
struct cue_state { const az_core_v1 *core; const az_library_v1 *lib; uint64_t sequence,highwater,commands,worker_started_ns,worker_idle_deadline_ns,worker_drain_deadline_ns; struct cue_input active; int pending,terminal,io_failed; atomic_uint ui_ready; };
static struct cue_state cue;
static uint64_t cue_now(void) {
#ifdef CUE_TEST_NOW
 return CUE_TEST_NOW();
#else
 struct timespec t={0}; if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0)return 0;
 return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
#endif
}
static int cue_worker_begin(uint64_t now) {
 if(!now||now>UINT64_MAX-CUE_LIFETIME_NS)return 0;
 cue.worker_started_ns=now;cue.worker_idle_deadline_ns=now+CUE_IDLE_NS;cue.worker_drain_deadline_ns=0;return 1;
}
static void cue_note_activity(uint64_t now) {
 if(cue.worker_drain_deadline_ns)return;
 if(!cue.worker_started_ns){if(!cue_worker_begin(now))cue.io_failed=1;return;}
 if(!now||now>UINT64_MAX-CUE_IDLE_NS){cue.io_failed=1;return;}
 cue.worker_idle_deadline_ns=now+CUE_IDLE_NS;
}
static int cue_worker_continue(uint64_t now) {
 if(!now||!cue.worker_started_ns||compare_timestamps(now,cue.worker_started_ns,1000000000,1000000000,1)==AZ_TIME_BEFORE)return 0;
 int closing=cue.io_failed||cue.sequence>=8192||cue.worker_drain_deadline_ns||
  compare_timestamps(now,cue.worker_idle_deadline_ns,1000000000,1000000000,1)>=AZ_TIME_EQUAL||
  compare_timestamps(now,cue.worker_started_ns+CUE_LIFETIME_NS,1000000000,1000000000,1)>=AZ_TIME_EQUAL;
 if(!closing)return 1;
 if(!cue.pending)return 0;
 if(!cue.worker_drain_deadline_ns){if(now>UINT64_MAX-CUE_DRAIN_NS)return 0;cue.worker_drain_deadline_ns=now+CUE_DRAIN_NS;}
 return compare_timestamps(now,cue.worker_drain_deadline_ns,1000000000,1000000000,1)==AZ_TIME_BEFORE;
}
static int cue_integer(const char *s,uint64_t *out) { uint64_t n=0;if(!*s||(s[0]=='0'&&s[1]))return 0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(UINT64_MAX-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');}*out=n;return 1; }
static int cue_number(const char *s,double *out) {
 if(!*s||strspn(s,"0123456789.eE+-")!=strlen(s))return 0;
 char *end;errno=0;double v=strtod(s,&end);if(*end||errno||!isfinite(v))return 0;*out=v;return 1;
}
static int cue_nibble(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
static int cue_comment(const char *s,char out[128]) {
 memset(out,0,128);if(!strcmp(s,"-"))return 1;size_t n=strlen(s);if(!n||n%2||n>254)return 0;
 for(size_t i=0;i<n;i+=2){int a=cue_nibble(s[i]),b=cue_nibble(s[i+1]);if(a<0||b<0||!(a*16+b))return 0;out[i/2]=(char)(a*16+b);}return 1;
}
static void cue_hex(const char in[128],char out[257]) {static const char hex[]="0123456789abcdef";size_t n=strnlen(in,128);if(!n){strcpy(out,"-");return;}for(size_t i=0;i<n;i++){unsigned v=(unsigned char)in[i];out[i*2]=hex[v>>4];out[i*2+1]=hex[v&15];}out[n*2]=0;}
static int cue_parse(const char *line,size_t n,struct cue_input *out) {
 static const char *keys[]={"v=","pid=","action=","id=","deck=","op=","load=","revision=","timeout_ns=","flags=","classification=","entry_id=","position=","end=","beats=","rgb=","comment_hex="};
 if(!line||!out||!n||n>1023||line[n-1]!='\n'||memchr(line,0,n))return 0;
 char copy[1024],*values[17],*at=copy;memcpy(copy,line,n-1);copy[n-1]=0;
 for(unsigned i=0;i<17;i++){size_t k=strlen(keys[i]);if(strncmp(at,keys[i],k))return 0;values[i]=at+k;char *space=strchr(values[i],' ');if(i<16){if(!space||space==values[i])return 0;*space=0;at=space+1;}else if(space)return 0;}
 uint64_t v[17]={0};unsigned integer[]={0,1,3,4,5,6,7,8,9,10,11,15};
 for(unsigned i=0;i<sizeof integer/sizeof *integer;i++)if(!cue_integer(values[integer[i]],&v[integer[i]]))return 0;
 if(v[0]!=1||v[4]>3||v[5]>UINT32_MAX||v[9]>UINT32_MAX||v[10]>UINT32_MAX||v[11]>UINT32_MAX||v[15]>UINT32_MAX||strlen(values[2])>8)return 0;
 if(strcmp(values[2],"submit")&&strcmp(values[2],"snapshot")&&strcmp(values[2],"state")&&strcmp(values[2],"cancel"))return 0;
 memset(out,0,sizeof *out);out->pid=v[1];out->id=v[3];out->deck=(uint32_t)v[4];out->op=(uint32_t)v[5];out->load=v[6];out->revision=v[7];out->timeout=v[8];out->flags=(uint32_t)v[9];out->classification=(uint32_t)v[10];out->entry=(uint32_t)v[11];out->rgb=(uint32_t)v[15];strcpy(out->action,values[2]);
 return cue_number(values[12],&out->position)&&cue_number(values[13],&out->end)&&cue_number(values[14],&out->beats)&&cue_comment(values[16],out->comment);
}
static int cue_path(char *out,size_t n,const char *leaf) {const char *d=cue.core->data_dir(ID);if(!d||d[0]!='/'||strlen(d)>300||strpbrk(d," \t\r\n"))return 0;int k=snprintf(out,n,"%s/%s",d,leaf);return k>0&&(size_t)k<n;}
static int cue_write(const char *line,size_t length) {
 char path[512],leaf[64];snprintf(leaf,sizeof leaf,"results.%ld.log",(long)getpid());if(!cue_path(path,sizeof path,leaf))return 0;
 int fd=open(path,O_WRONLY|O_APPEND|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return 0;struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&st.st_size>=0&&(uint64_t)st.st_size+length<=LIMIT;
 if(ok)ok=write(fd,line,length)==(ssize_t)length;
 if(close(fd))ok=0;
 if(!ok)cue.io_failed=1;
 return ok;
}
static int cue_result(const az_command_result *r,uint64_t id) {return r&&r->size>=sizeof *r&&r->version==AZ_DATA_VERSION&&r->request_id==id&&r->outcome<=AZ_COMMAND_UNCERTAIN;}
static void cue_emit(const char *event,const struct cue_input *v,int rc,const az_command_result *r,int entries) {
 az_deck_snapshot s={.size=sizeof s};int sr=cue.lib->deck_snapshot((int)v->deck,&s);int load=sr==AZ_OK&&s.version==AZ_DATA_VERSION&&(s.valid&AZ_LIVE_NATIVE_LOAD);
 az_native_cue_snapshot c={.size=sizeof c};int cr=entries?cue.lib->native_cues((int)v->deck,&c):AZ_UNSUPPORTED;
 if(cr==AZ_OK&&(c.size<sizeof c||c.version!=AZ_DATA_VERSION||c.deck!=v->deck||c.count>AZ_NATIVE_CUE_LIMIT))cr=AZ_INVALID;
 unsigned count=cr==AZ_OK?c.count:0;
 uint64_t seq=++cue.sequence,now=cue_now();char line[1536];
 int n=snprintf(line,sizeof line,"SDK_CUE v=1 seq=%llu event=%s id=%llu deck=%u op=%u target_load=%llu target_revision=%llu flags=%u classification=%u entry_id=%u rgb=%u timeout_ns=%llu submitted_ns=%llu deadline_ns=%llu rc=%d outcome=%d reason=%d revision=%llu receipt_ns=%llu query_ns=%llu load_valid=%d native_load=%llu load_unit=%llu load_ns=%llu loop_valid=%d engaged=%d loop_in=%.17g loop_out=%.17g main_valid=%u main_s=%.17g cues_rc=%d cues_load=%llu cues_rev=%llu cue_count=%u\n",(unsigned long long)seq,event,(unsigned long long)v->id,v->deck,v->op,(unsigned long long)v->load,(unsigned long long)v->revision,v->flags,v->classification,v->entry,v->rgb,(unsigned long long)v->timeout,(unsigned long long)v->submitted_ns,(unsigned long long)v->deadline,rc,r?(int)r->outcome:-1,r?r->reason:0,(unsigned long long)(r?r->revision:0),(unsigned long long)(r?r->observed_ns:0),(unsigned long long)now,!!load,(unsigned long long)(load?s.native_load_generation:0),(unsigned long long)(load?s.native_load_unit_generation:0),(unsigned long long)(load?s.native_load_observed_ns:0),sr==AZ_OK&&!!(s.valid&AZ_LIVE_LOOP_BOUNDS)&&!!(s.loop.valid&AZ_LOOP_BOUNDS),sr==AZ_OK&&(s.valid&AZ_LIVE_LOOP_ENGAGED)?s.loop.engaged:-1,sr==AZ_OK&&(s.valid&AZ_LIVE_LOOP_BOUNDS)&&(s.loop.valid&AZ_LOOP_BOUNDS)?s.loop.in_s:0,sr==AZ_OK&&(s.valid&AZ_LIVE_LOOP_BOUNDS)&&(s.loop.valid&AZ_LOOP_BOUNDS)?s.loop.out_s:0,sr==AZ_OK?s.main_cue.valid:0,sr==AZ_OK&&(s.main_cue.valid&AZ_CUE_START)?s.main_cue.start_s:0,cr,(unsigned long long)(cr==AZ_OK?c.native_load_generation:0),(unsigned long long)(cr==AZ_OK?c.revision:0),count);
 if(n>0&&(size_t)n<sizeof line)cue_write(line,(size_t)n);
 if(cr==AZ_OK)for(unsigned i=0;i<c.count;i++){az_cue_entry *e=&c.entries[i];char comment[257];cue_hex(e->comment,comment);n=snprintf(line,sizeof line,"SDK_CUE_ENTRY v=1 snapshot_seq=%llu deck=%u load=%llu revision=%llu classification=%u entry_id=%u slot=%u valid=%u type=%u start=%.17g end=%.17g rgb=%u color_id=%u saved=%d modified=%d auto_active=%d comment_hex=%s\n",(unsigned long long)seq,v->deck,(unsigned long long)c.native_load_generation,(unsigned long long)c.revision,e->classification,e->entry_id,e->slot,e->valid,e->type,e->valid&AZ_CUE_START?e->start_s:0,e->valid&AZ_CUE_END?e->end_s:0,e->rgb,e->color_id,e->saved,e->modified,e->auto_active,comment);if(n>0&&(size_t)n<sizeof line)cue_write(line,(size_t)n);}
}
static int cue_handle(const struct cue_input *v) {
 uint64_t now=cue_now();if(v->pid!=(uint64_t)getpid()||!now){cue_emit("refused",v,AZ_STALE,NULL,0);return AZ_STALE;}
 if(!strcmp(v->action,"snapshot")||!strcmp(v->action,"state")){if(v->id||v->op||v->load||v->timeout||v->revision||v->flags||v->classification||v->entry||v->rgb||v->position||v->end||v->beats||v->comment[0]){cue_emit("refused",v,AZ_INVALID,NULL,0);return AZ_INVALID;}cue_note_activity(now);cue_emit("snapshot",v,AZ_OK,NULL,!strcmp(v->action,"snapshot"));return AZ_OK;}
 if(!strcmp(v->action,"cancel")){if(!cue.pending||v->id!=cue.active.id||v->deck!=cue.active.deck||v->op||v->load||v->timeout||v->revision||v->flags||v->classification||v->entry||v->rgb||v->position||v->end||v->beats||v->comment[0]){cue_emit("refused",v,AZ_INVALID,NULL,0);return AZ_INVALID;}az_command_result r={.size=sizeof r};int rc=cue.lib->command_cancel(ID,v->id,&r);cue_emit("cancel",&cue.active,rc,cue_result(&r,v->id)?&r:NULL,0);if(rc==AZ_OK&&cue_result(&r,v->id))cue.terminal=r.outcome!=AZ_COMMAND_ACCEPTED;return rc;}
 if(!v->id||!v->load||!v->timeout||compare_timestamps(v->timeout,UINT64_C(30000000000),1000000000,1000000000,1)==AZ_TIME_AFTER||v->op<1||v->op>26){cue_emit("refused",v,AZ_INVALID,NULL,0);return AZ_INVALID;}
 if(v->id<=cue.highwater){cue_emit("refused",v,AZ_STALE,NULL,0);return AZ_STALE;}if(cue.pending){cue_emit("refused",v,AZ_BUSY,NULL,0);return AZ_BUSY;}cue.highwater=v->id;
 az_data_capabilities caps={.size=sizeof caps};if(cue.lib->data_capabilities(&caps)!=AZ_OK||!(caps.commands&(UINT64_C(1)<<v->op))){cue_emit("refused",v,AZ_UNSUPPORTED,NULL,0);return AZ_UNSUPPORTED;}
 struct cue_input prepared=*v;prepared.submitted_ns=cue_now();if(!prepared.submitted_ns||prepared.submitted_ns>UINT64_MAX-prepared.timeout){cue_emit("refused",v,AZ_INVALID,NULL,0);return AZ_INVALID;}prepared.deadline=prepared.submitted_ns+prepared.timeout;v=&prepared;cue_note_activity(prepared.submitted_ns);
 az_command_request request={.size=sizeof request,.version=AZ_DATA_VERSION,.operation=v->op,.deck=v->deck,.request_id=v->id,.expected_load_generation=v->load,.expected_revision=v->revision,.deadline_ns=v->deadline,.quantization=AZ_QUANTIZE_NATIVE,.flags=v->flags,.classification=v->classification,.entry_id=v->entry,.position_s=v->position,.end_s=v->end,.beats=v->beats,.rgb=v->rgb};memcpy(request.comment,v->comment,sizeof request.comment);
 az_command_result r={.size=sizeof r};int rc=cue.lib->command_submit(ID,&request,&r);cue_emit("submit",v,rc,cue_result(&r,v->id)?&r:NULL,0);
 if(rc==AZ_OK&&cue_result(&r,v->id)){cue.active=*v;cue.pending=1;cue.terminal=r.outcome!=AZ_COMMAND_ACCEPTED;}return rc;
}
static void cue_step(void) {
 az_data_capabilities caps={.size=sizeof caps};if(cue.lib->data_capabilities(&caps)==AZ_OK&&caps.commands!=cue.commands){cue.commands=caps.commands;cue.core->log(ID,"SDK_CUE_READY v=1 pid=%ld commands=%llu data=%s",(long)getpid(),(unsigned long long)cue.commands,cue.core->data_dir(ID));}
 if(cue.pending){az_command_result r={.size=sizeof r};if(!cue.terminal){int rc=cue.lib->command_poll(ID,cue.active.id,&r);cue_emit("poll",&cue.active,rc,cue_result(&r,cue.active.id)?&r:NULL,0);if(rc==AZ_OK&&cue_result(&r,cue.active.id))cue.terminal=r.outcome!=AZ_COMMAND_ACCEPTED;}
  if(cue.terminal){int rc=cue.lib->command_release(ID,cue.active.id);cue_emit("release",&cue.active,rc,NULL,0);if(rc==AZ_OK)cue.pending=cue.terminal=0;return;}}
 if(cue.io_failed)return;
 char input[512],processing[512];if(!cue_path(input,sizeof input,"request")||!cue_path(processing,sizeof processing,"request.processing"))return;
 if(rename(input,processing))return;
 int fd=open(processing,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);char line[1024];ssize_t n=-1;struct stat st;
 if(fd>=0){if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&st.st_size>0&&st.st_size<=1023)n=read(fd,line,sizeof line);close(fd);}unlink(processing);
 struct cue_input v={0};if(n<=0||!cue_parse(line,(size_t)n,&v))cue_emit("refused",&v,AZ_INVALID,NULL,0);else cue_handle(&v);
}
/* Native construction precedes the first SDK UI tick. Keep the test worker
 * idle until that real dispatch callback, so its filesystem/capability polling
 * does not compete with startup. All SDK I/O still runs on the worker. */
static void cue_tick(void) { atomic_store_explicit(&cue.ui_ready,1,memory_order_release); }
static void cue_worker_step(void) { if(atomic_load_explicit(&cue.ui_ready,memory_order_acquire))cue_step(); }
/* Active tests refresh the idle lease, while the two-hour hard cap and output
 * bounds remain fixed. Expiry drains an accepted request without changing its
 * action deadline or admitting another submit after that request is released. */
static void *cue_worker(void *unused) {(void)unused;while(cue_worker_continue(cue_now())){cue_worker_step();struct timespec t={0,50000000};if(nanosleep(&t,NULL)&&errno!=EINTR)break;}cue.core->log(ID,"SDK_CUE_DONE records=%llu pending=%d",(unsigned long long)cue.sequence,cue.pending);return NULL;}
static int init(const az_core_v1 *core) {
 if(!core||core->abi!=AZ_MOD_ABI||!AZ_HAS(core,data_dir)||!AZ_HAS(core,thread_start)||!AZ_HAS(core,library_service)||!AZ_HAS(core,log))return -1;
 const az_library_v1 *l=core->library_service(AZ_LIBRARY_ABI);if(!AZ_LIB_HAS(l,data_capabilities)||!AZ_LIB_HAS(l,deck_snapshot)||!AZ_LIB_HAS(l,native_cues)||!AZ_LIB_HAS(l,command_submit)||!AZ_LIB_HAS(l,command_poll)||!AZ_LIB_HAS(l,command_cancel)||!AZ_LIB_HAS(l,command_release))return -1;
 memset(&cue,0,sizeof cue);atomic_init(&cue.ui_ready,0);cue.core=core;cue.lib=l;az_data_capabilities caps={.size=sizeof caps};if(l->data_capabilities(&caps)!=AZ_OK||!cue_worker_begin(cue_now()))return -1;cue.commands=caps.commands;
 char file[512],leaf[64];snprintf(leaf,sizeof leaf,"results.%ld.log",(long)getpid());if(!cue_path(file,sizeof file,leaf))return -1;int fd=open(file,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK,0600);if(fd<0)return -1;struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode);if(close(fd))ok=0;if(!ok)return -1;
 core->log(ID,"SDK_CUE_READY v=1 pid=%ld commands=%llu data=%s",(long)getpid(),(unsigned long long)cue.commands,core->data_dir(ID));return core->thread_start(ID,"sdk-cue-command",cue_worker,NULL)==0?0:-1;
}
static const char *const tags[]={"any",NULL};static const az_mod_v1 mod={.abi=AZ_MOD_ABI,.size=sizeof mod,.id=ID,.fw_tags=tags,.init=init,.tick=cue_tick};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void){return &mod;}
