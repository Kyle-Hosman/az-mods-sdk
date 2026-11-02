/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SLIP_CAPTURE_H
#define SLIP_CAPTURE_H
#include <stdint.h>
#include "native/common/azcacheentry.h"
#ifdef __cplusplus
extern "C" {
#endif
struct slip_capture_record {
    uint64_t sequence, observed_ns;
    uintptr_t receiver, caller;
    uint32_t raw, reserved;
};
struct slip_capture_stats { uint64_t saved, dropped, rejected; };
/* One nonzero exact native return PC, armed once for this process lifetime.
 * Records contain opaque copied scalars, never enabled/deck/owner semantics.
 * Sequences1..512; the128-slot ring retains only the latest saved128 records.
 * Reads are bounded and leave output unchanged when absent/overwritten/busy.
 * dropped/rejected are bounded best-effort saturating diagnostic counters. */
int slip_capture_arm(uintptr_t exact_caller_pc);
int slip_capture_read(uint64_t sequence, struct slip_capture_record *);
void slip_capture_get_stats(struct slip_capture_stats *);
#ifdef __cplusplus
}
#endif
#endif
