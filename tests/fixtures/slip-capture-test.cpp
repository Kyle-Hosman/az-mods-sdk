/* SPDX-License-Identifier: GPL-3.0-only */
/* Host invokes only the original-frame boundary double. Entry0/firmware ABI,
 * source ownership and native Slip meaning are not supplied by this fixture. */
#include "capture.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <time.h>

#ifdef SLIP_CAPTURE_FORWARD_ONLY
extern "C" {
void *azsdk_cache_entry_originals[AZCE_KIND_COUNT];
int slip_capture_arm(uintptr_t pc) { static uintptr_t armed; if (!pc||armed)return 0;armed=pc;return 1; }
int slip_capture_read(uint64_t, slip_capture_record *) { return 0; }
void slip_capture_get_stats(slip_capture_stats *out) { if (out) *out={0,0,0}; }
void azsdk_cache_dispatch_call(azce_call *call) { if (call) azsdk_cache_invoke_original(call); }
}
#endif

#if defined(__aarch64__) && defined(__linux__) && !defined(SLIP_CAPTURE_HOST_BOUNDARY) && !defined(SLIP_CAPTURE_FORWARD_ONLY)
/* One affected entry only. CFI preserves exception propagation across this
 * real BL caller, entry0 and the qualified invoke-original assembly bridge. */
struct ArmResult { uint64_t low, high; };
struct ArmCall { uintptr_t receiver=1; uint32_t raw=255; unsigned calls=0; bool throwing=false; };
struct ArmException { unsigned tag; };
using ArmFunction=ArmResult (*)(uintptr_t,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,ArmCall *);
extern "C" ArmResult slip_fixture_entry_call(uintptr_t,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,ArmCall *);
extern "C" char slip_fixture_return_pc;
extern "C" void azsdk_cache_entry_0(void);
asm(
".text\n.p2align 2\n.global slip_fixture_entry_call\n.type slip_fixture_entry_call,%function\n"
"slip_fixture_entry_call:\n.cfi_startproc\nstp x29,x30,[sp,#-16]!\n.cfi_def_cfa_offset 16\n"
".cfi_offset 29,-16\n.cfi_offset 30,-8\nmov x29,sp\n.cfi_def_cfa_register 29\n"
"bl azsdk_cache_entry_0\n.global slip_fixture_return_pc\nslip_fixture_return_pc:\n"
"ldp x29,x30,[sp],#16\n.cfi_def_cfa sp,0\n.cfi_restore 29\n.cfi_restore 30\nret\n"
".cfi_endproc\n.size slip_fixture_entry_call,.-slip_fixture_entry_call\n");
extern "C" ArmResult slip_fixture_original(uintptr_t receiver,uint32_t raw,uint64_t a2,uint64_t a3,
                                           uint64_t a4,uint64_t a5,uint64_t a6,ArmCall *call) {
    assert(call&&receiver==call->receiver&&raw==call->raw&&errno==EINTR);
    assert(a2==UINT64_C(0x1234567800000002)&&a3==UINT64_C(0x1234567800000003)&&
           a4==UINT64_C(0x1234567800000004)&&a5==UINT64_C(0x1234567800000005)&&a6==UINT64_C(0x1234567800000006));
    ++call->calls;errno=ERANGE;
    if(call->throwing)throw ArmException{0x130200};
    return {UINT64_C(0xdeadbeef87654321),UINT64_C(0x123456789abcdef0)};
}
extern "C" int slip_capture_test_clock_gettime(clockid_t clock,struct timespec *out) {
    static unsigned tick;
    assert(clock==CLOCK_MONOTONIC);errno=EIO;out->tv_sec=1;out->tv_nsec=++tick;return 0;
}
static void arm_invoke(ArmCall &call,ArmFunction fn=slip_fixture_entry_call) {
    errno=EINTR;
    ArmResult result=fn(call.receiver,call.raw,UINT64_C(0x1234567800000002),UINT64_C(0x1234567800000003),
                        UINT64_C(0x1234567800000004),UINT64_C(0x1234567800000005),UINT64_C(0x1234567800000006),&call);
    assert(call.calls==1&&errno==ERANGE&&result.low==UINT64_C(0xdeadbeef87654321)&&result.high==UINT64_C(0x123456789abcdef0));
}
int main(int argc,char **argv) {
    assert(argc==2);
    azsdk_cache_entry_originals[0]=reinterpret_cast<void *>(slip_fixture_original);
    ArmCall baseline;arm_invoke(baseline);
    std::puts("SLIP_NATIVE_FORWARD_BASELINE_OK actual ARM entry0 args/results/errno");std::fflush(stdout);
    uintptr_t caller=reinterpret_cast<uintptr_t>(&slip_fixture_return_pc);
    assert(!slip_capture_arm(0)&&slip_capture_arm(caller)&&!slip_capture_arm(caller));
    const uint32_t values[]={0,1,255,UINT32_MAX};
    for(unsigned i=0;i<4;++i) {
        ArmCall call;call.receiver=i%2?UINTPTR_MAX:1;call.raw=values[i];arm_invoke(call);
        slip_capture_record out={};assert(slip_capture_read(i+1,&out)&&"missing by-value Slip capture record after native forwarding");
        assert(out.sequence==i+1&&out.observed_ns&&out.receiver==call.receiver&&out.caller==caller&&out.raw==values[i]&&!out.reserved);
    }
    ArmCall throwing;throwing.throwing=true;bool caught=false;
    try{arm_invoke(throwing);}catch(const ArmException &e){caught=e.tag==0x130200;}
    assert(caught&&throwing.calls==1&&errno==ERANGE);
    slip_capture_record out={};assert(slip_capture_read(5,&out)&&out.raw==255&&out.caller==caller);
    ArmCall wrong;arm_invoke(wrong,reinterpret_cast<ArmFunction>(azsdk_cache_entry_0));
    slip_capture_stats stats={};slip_capture_get_stats(&stats);
    assert(stats.saved==5&&stats.rejected==2&&!slip_capture_read(6,&out));
    std::printf("PASS slip capture %s (actual AArch64 entry0+original/CFI exception; raw observation only)\n",argv[1]);
}
#else
namespace {
constexpr uintptr_t caller=0x130200;
std::atomic<uint64_t> ticks(1000000000);
unsigned clock_mode, original_calls;
bool reentered;
struct NativeException { unsigned tag; };
struct Frame {
    azce_call call={}, before={};
    unsigned calls=0;
    int entry_errno=EINTR, result_errno=ERANGE;
    bool throwing=false;
    Frame(uint32_t raw=255, uintptr_t receiver=1) {
        call.kind=AZCE_KIND_MESSAGE; call.caller_link=caller;
        call.caller_x21=UINT64_C(0x8765432198765432);
        for(unsigned i=0;i<9;++i)call.args[i]=UINT64_C(0x9876000000)+i;
        call.args[0]=receiver;call.args[1]=UINT64_C(0xfedcba9800000000)|raw;
        call.args[7]=reinterpret_cast<uintptr_t>(this);
        call.original=UINT64_C(0x12345678);
        for(unsigned i=0;i<8;++i)call.vector_args[i]={{UINT64_C(0xabcdef1234000000)+i,UINT64_C(0x7890123400000000)+i}};
        before=call;
    }
    void check_inputs() const {
        assert(!std::memcmp(call.args,before.args,sizeof call.args));
        assert(!std::memcmp(call.vector_args,before.vector_args,sizeof call.vector_args));
        assert(call.kind==before.kind&&call.reserved==before.reserved&&call.original==before.original&&call.caller_link==before.caller_link&&
               call.caller_x21==before.caller_x21&&call.caller_reserved==before.caller_reserved);
    }
    void results() const {
        check_inputs();assert(calls==1&&errno==result_errno);
        assert(call.result[0]==UINT64_C(0xdeadbeef87654321)&&call.result[1]==UINT64_C(0x123456789abcdef0));
        for(unsigned i=0;i<4;++i)assert(call.vector_results[i].words[0]==100+i&&call.vector_results[i].words[1]==200+i);
    }
    void invoke() { before=call;errno=entry_errno;azsdk_cache_dispatch_call(&call);results(); }
};
void baseline() {
    Frame frame;frame.invoke();
    std::puts("SLIP_NATIVE_FORWARD_BASELINE_OK args/results/errno preserved");std::fflush(stdout);
}
slip_capture_stats stats() {slip_capture_stats s={};slip_capture_get_stats(&s);return s;}
void absent(uint64_t sequence) {
    slip_capture_record out;std::memset(&out,0xa5,sizeof out);auto before=out;
    assert(!slip_capture_read(sequence,&out));assert(!std::memcmp(&out,&before,sizeof out));
}
}
extern "C" void azsdk_cache_invoke_original(azce_call *call) {
    assert(call);auto &frame=*reinterpret_cast<Frame *>(call->args[7]);
    assert(&frame.call==call&&errno==frame.entry_errno);frame.check_inputs();
    ++frame.calls;++original_calls;
    call->result[0]=UINT64_C(0xdeadbeef87654321);call->result[1]=UINT64_C(0x123456789abcdef0);
    for(unsigned i=0;i<4;++i)call->vector_results[i]={{100+i,200+i}};
    errno=frame.result_errno;
    if(frame.throwing)throw NativeException{0x130200};
}
extern "C" int slip_capture_test_clock_gettime(clockid_t clock,struct timespec *out) {
    assert(clock==CLOCK_MONOTONIC);errno=EIO;
    if(clock_mode==1)return -1;
    if(clock_mode==2){out->tv_sec=-1;out->tv_nsec=0;return 0;}
    if(clock_mode==3){out->tv_sec=1;out->tv_nsec=1000000000;return 0;}
    if(clock_mode==4){out->tv_sec=0;out->tv_nsec=0;return 0;}
    if(clock_mode==5){out->tv_sec=std::numeric_limits<time_t>::max();out->tv_nsec=0;return 0;}
    if(clock_mode==6){out->tv_sec=0;out->tv_nsec=1;return 0;}
    if(clock_mode==7&&!reentered){reentered=true;Frame inner(1,UINTPTR_MAX);inner.invoke();}
    uint64_t ns=ticks.fetch_add(1);out->tv_sec=static_cast<time_t>(ns/1000000000);out->tv_nsec=static_cast<long>(ns%1000000000);return 0;
}
int main(int argc,char **argv) {
    assert(argc==2);baseline();
    assert(!slip_capture_arm(0));assert(slip_capture_arm(caller));assert(!slip_capture_arm(caller));assert(!slip_capture_arm(caller+4));
    if(!std::strcmp(argv[1],"capture")) {
        const uint32_t raw[]={0,1,255,UINT32_MAX};
        for(unsigned i=0;i<4;++i) {
            Frame frame(raw[i],i%2?UINTPTR_MAX:1);frame.invoke();
            slip_capture_record out={};
            assert(slip_capture_read(i+1,&out)&&"missing by-value Slip capture record after native forwarding");
            assert(out.sequence==i+1&&out.observed_ns==UINT64_C(1000000000)+i&&out.receiver==frame.call.args[0]&&out.caller==caller&&out.raw==raw[i]&&!out.reserved);
        }
        assert(stats().saved==4);absent(0);absent(5);absent(UINT64_MAX);assert(!slip_capture_read(1,nullptr));slip_capture_get_stats(nullptr);
    } else if(!std::strcmp(argv[1],"reject")) {
        auto before=stats();
        Frame wrong;wrong.call.caller_link=caller+4;wrong.invoke();
        Frame reserved;reserved.call.reserved=1;reserved.invoke();
        Frame caller_reserved;caller_reserved.call.caller_reserved=1;caller_reserved.invoke();
        Frame kind;kind.call.kind=AZCE_KIND_SELECTION;kind.invoke();
        assert(stats().saved==0&&stats().rejected==before.rejected+4);absent(1);
        Frame valid;valid.invoke();assert(stats().saved==1);
    } else if(!std::strcmp(argv[1],"clock")) {
        for(clock_mode=1;clock_mode<=5;++clock_mode){Frame frame;frame.invoke();assert(!stats().saved);absent(1);}
        clock_mode=0;Frame valid;valid.invoke();assert(stats().saved==1);
        clock_mode=6;Frame backwards;backwards.invoke();assert(stats().saved==1&&stats().dropped==6);absent(2);
    } else if(!std::strcmp(argv[1],"reentrant")) {
        clock_mode=7;Frame frame;frame.invoke();assert(reentered&&stats().saved==1&&stats().dropped==1&&original_calls==3);
        slip_capture_record out={};assert(slip_capture_read(1,&out)&&out.raw==255&&out.receiver==1);absent(2);
    } else if(!std::strcmp(argv[1],"throws")) {
        Frame frame;frame.throwing=true;bool caught=false;
        errno=frame.entry_errno;
        try {azsdk_cache_dispatch_call(&frame.call);}catch(const NativeException &e){caught=e.tag==0x130200;}
        assert(caught);frame.results();assert(stats().saved==1);
        Frame second;second.invoke();assert(stats().saved==2);
    } else if(!std::strcmp(argv[1],"ring")) {
        std::atomic<bool> done(false);std::atomic<unsigned> checked(0);
        for(uint64_t sequence=1;sequence<=128;++sequence){Frame frame(static_cast<uint32_t>(sequence*31337),UINTPTR_MAX-sequence);frame.invoke();}
        std::thread reader([&]{
            for(unsigned round=0;round<20000&&!done.load();++round)for(uint64_t sequence=1;sequence<=512;++sequence){
                slip_capture_record out={};if(!slip_capture_read(sequence,&out))continue;
                assert(out.sequence==sequence&&out.raw==static_cast<uint32_t>(sequence*31337)&&out.receiver==UINTPTR_MAX-sequence&&out.caller==caller&&!out.reserved&&out.observed_ns);
                ++checked;
            }
        });
        while(!checked.load())std::this_thread::yield();
        for(uint64_t sequence=129;sequence<=512;++sequence){Frame frame(static_cast<uint32_t>(sequence*31337),UINTPTR_MAX-sequence);frame.invoke();if(!(sequence%8))std::this_thread::yield();}
        done.store(true);reader.join();
        assert(stats().saved==512);
        for(uint64_t sequence=1;sequence<=384;++sequence)absent(sequence);
        for(uint64_t sequence=385;sequence<=512;++sequence){slip_capture_record out={};assert(slip_capture_read(sequence,&out)&&out.sequence==sequence&&out.raw==sequence*31337);}
        for(unsigned i=0;i<5;++i){Frame frame;frame.invoke();}assert(stats().saved==512&&stats().dropped==5);absent(513);absent(UINT64_MAX);
    } else assert(!"unknown fixture mode");
    std::printf("PASS slip capture %s (host original-boundary double only)\n",argv[1]);
}
#endif
