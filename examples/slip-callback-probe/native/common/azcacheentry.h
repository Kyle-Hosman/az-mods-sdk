/* SPDX-License-Identifier: GPL-3.0-only */
/* Private register-only AAPCS64 entry contract for source-cache observers.
 * This is SDK scaffolding, not a recovered native C++ type or a public API.
 * Reviewed entries may use up to8 integer/pointer and8 FP/SIMD argument
 * registers, with4 FP/SIMD result registers. Stack arguments remain unsupported.
 * Indirect results retain original X8; register preservation proves no native
 * object lifetime, signature, semantic return type or source association.
 */
#ifndef AZCACHEENTRY_H
#define AZCACHEENTRY_H
#define AZCE_KIND_MESSAGE 0
#define AZCE_KIND_SELECTION 1
#define AZCE_KIND_CREATE 2
#define AZCE_KIND_QUERY 3
#define AZCE_KIND_GENERIC 4
#define AZCE_KIND_TRACKINFO 5
#define AZCE_KIND_SETUP 6
#define AZCE_KIND_LAYOUT 7
#define AZCE_KIND_CLEAR 8
#define AZCE_KIND_CACHE_DESTROY 9
#define AZCE_KIND_POOL_GROW 10
#define AZCE_KIND_PRIMARY_FACTORY 11
#define AZCE_KIND_STANDARD_FACTORY 12
#define AZCE_KIND_COLLECTOR_DISPOSE 13
#define AZCE_KIND_SELECTION_STORE 14
#define AZCE_KIND_SELECTION_CLEAR 15
#define AZCE_KIND_SERVER_RESET 16
#define AZCE_KIND_SERVER_DESTROY 17
#define AZCE_KIND_LOADINFO_DESTROY 18
#define AZCE_KIND_COLLECTOR_DESTROY 19
#define AZCE_KIND_COLLECTOR_DELETE 20
#define AZCE_KIND_REPLY_TICKET 21
#define AZCE_KIND_REPLY_RECEPTION 22
#define AZCE_KIND_COUNT 23
#define AZCE_ARGS 0
#define AZCE_X8 64
#define AZCE_RESULTS 72
#define AZCE_ORIGINAL 88
#define AZCE_KIND 96
#define AZCE_CALLER_LINK 104
#define AZCE_VARGS 112
#define AZCE_VRESULTS 240
#define AZCE_CALLER_X21 304
#define AZCE_SIZE 320
#define AZCE_STACK 336
#define AZCE_SAVED_FP 320
#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct azce_vector { uint64_t words[2]; } __attribute__((aligned(16)));
struct azce_call {
    uint64_t args[9];
    uint64_t result[2];
    uintptr_t original;
    uint32_t kind, reserved;
    uint64_t caller_link;
    struct azce_vector vector_args[8], vector_results[4];
    /* Original incoming callee-saved X21, before a C++ dispatcher can reuse it.
     * This is only an ABI observation. A native ownership witness additionally
     * requires the exact checked promotion call site and a genuine weak owner. */
    uint64_t caller_x21, caller_reserved;
};
/* The installer must publish each original before redirecting native entry.
 * These are original-function slots, not native address/profile mappings. */
__attribute__((visibility("hidden"))) extern void *azsdk_cache_entry_originals[AZCE_KIND_COUNT];
__attribute__((visibility("hidden"))) void *azsdk_cache_entry(unsigned kind);
/* Startup-only, AFTER the canonical installer has verified every byte and
 * installed every required entry. Validates distinct callable originals and
 * publishes cache-observer admission; this proves no source/load/command
 * semantics. No reset/uninstall. Originals/code must remain process-lifetime. */
__attribute__((visibility("hidden"))) int azsdk_cache_capture_admit(uint32_t installed_mask);
__attribute__((visibility("hidden"))) int azsdk_cache_capture_ready(void);
__attribute__((visibility("hidden"))) void azsdk_cache_invoke_original(struct azce_call *);
/* Strong production dispatcher supplies ownership scopes; tests may override
 * the forwarding-only weak default. It must let native exceptions propagate. */
__attribute__((visibility("hidden"))) void azsdk_cache_dispatch_call(struct azce_call *);
#ifdef __cplusplus
}
static_assert(sizeof(azce_call) == AZCE_SIZE && offsetof(azce_call, args[8]) == AZCE_X8 &&
              offsetof(azce_call, result) == AZCE_RESULTS && offsetof(azce_call, original) == AZCE_ORIGINAL &&
              offsetof(azce_call, kind) == AZCE_KIND && offsetof(azce_call, caller_link) == AZCE_CALLER_LINK &&
              offsetof(azce_call, vector_args) == AZCE_VARGS && offsetof(azce_call, vector_results) == AZCE_VRESULTS &&
              offsetof(azce_call, caller_x21) == AZCE_CALLER_X21 &&
              alignof(azce_call) == 16, "cache observer register-frame ABI");
#endif
#endif
#endif
