/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
static uint64_t virtual_now = UINT64_C(1000000000000);
static uint64_t virtual_clock(void) { return virtual_now; }
#define PROBE_TEST_NOW virtual_clock
#define main ordinary_fixture_main
#include "loop-command-probe.c"
#undef main
struct model_state {
    int engaged;
    double in, out;
    uint64_t revision, stamp;
};
static struct model_state actual_state, public_state;
static uint64_t actual_load = 42, applied_id, submit_times[32], applied_times[32];
static unsigned operations[32], call_count, delay_polls, remaining_polls, observer_delay, observer_left, fail_at;
static int immediate, unexpected_refusal;
static void apply(const az_command_request *r) {
    if (applied_id == r->request_id) return;
    applied_id = r->request_id;
    applied_times[call_count - 1] = virtual_now;
    switch (r->operation) {
    case AZ_CMD_LOOP_EXIT: actual_state.engaged = 0; break;
    case AZ_CMD_LOOP_OUT:
    case AZ_CMD_RELOOP:
    case AZ_CMD_LOOP_ENTER: actual_state.engaged = 1; break;
    case AZ_CMD_LOOP_DOUBLE: actual_state.out = actual_state.in + 2 * (actual_state.out - actual_state.in); break;
    case AZ_CMD_LOOP_HALVE: actual_state.out = actual_state.in + (actual_state.out - actual_state.in) / 2; break;
    case AZ_CMD_LOOP_LENGTH: actual_state.out = actual_state.in + r->beats * .5; break;
    case AZ_CMD_LOOP_ADJUST_IN: actual_state.in += r->position_s; break;
    case AZ_CMD_LOOP_ADJUST_OUT: actual_state.out += r->end_s; break;
    case AZ_CMD_LOOP_MOVE:
        actual_state.in += r->position_s;
        actual_state.out += r->position_s;
        break;
    default: return; /* IN/CLEAR have no public retained reader. */
    }
    actual_state.revision++;
    actual_state.stamp = virtual_now;
    observer_left = observer_delay;
    if (!observer_left) public_state = actual_state;
}
static int batch_snapshot(int deck, az_deck_snapshot *out) {
    if (observer_left && !--observer_left) {
        public_state = actual_state;
        public_state.stamp = virtual_now;
    }
    memset(out, 0, sizeof *out);
    out->size = sizeof *out;
    out->version = 1;
    out->deck = (unsigned) deck;
    out->valid = AZ_LIVE_NATIVE_LOAD | AZ_LIVE_LOOP_ENGAGED | (public_state.engaged ? AZ_LIVE_LOOP_BOUNDS : 0);
    out->native_load_generation = actual_load;
    out->native_load_unit_generation = 11;
    out->native_load_observed_ns = virtual_now;
    out->loop.valid = AZ_LOOP_ENGAGED | (public_state.engaged ? AZ_LOOP_BOUNDS : 0);
    out->loop.engaged = public_state.engaged;
    out->loop.in_s = public_state.in;
    out->loop.out_s = public_state.out;
    out->loop_unit_generation = 7;
    out->loop_revision = public_state.revision;
    out->loop_observed_ns = public_state.stamp;
    return AZ_OK;
}
static int batch_submit(const char *id, const az_command_request *r, az_command_result *out) {
    assert(!strcmp(id, ID) && r->flags == AZ_COMMAND_NATIVE_LOAD && r->quantization == AZ_QUANTIZE_NATIVE);
    assert(call_count < 32 && r->deadline_ns == virtual_now + UINT64_C(10000000000));
    assert(!r->expected_revision && !r->classification && !r->entry_id && !r->rgb && !r->comment[0]);
    submitted = *r;
    submit_times[call_count] = virtual_now;
    operations[call_count++] = r->operation;
    if (r->operation == AZ_CMD_LOOP_LENGTH) assert(r->beats == 8);
    if (r->operation == AZ_CMD_LOOP_MOVE || r->operation == AZ_CMD_LOOP_ADJUST_IN) assert(r->position_s == .05);
    if (r->operation == AZ_CMD_LOOP_ADJUST_OUT) assert(r->end_s == .05);
    if (r->expected_load_generation == UINT64_MAX && !unexpected_refusal) {
        result(out, r->request_id, AZ_COMMAND_REJECTED);
        out->reason = AZ_STALE;
        return AZ_STALE;
    }
    assert(r->expected_load_generation == 42 || unexpected_refusal);
    remaining_polls = delay_polls;
    if (immediate) {
        apply(r);
        result(out, r->request_id, AZ_COMMAND_APPLIED);
    } else result(out, r->request_id, AZ_COMMAND_ACCEPTED);
    return AZ_OK;
}
static int batch_poll(const char *id, uint64_t rid, az_command_result *out) {
    assert(!strcmp(id, ID) && rid == submitted.request_id);
    int outcome = terminal_outcome;
    if (virtual_now >= submitted.deadline_ns) outcome = AZ_COMMAND_UNCERTAIN;
    else if (remaining_polls) {
        remaining_polls--;
        outcome = AZ_COMMAND_ACCEPTED;
    } else if (fail_at && call_count == fail_at) outcome = AZ_COMMAND_UNCERTAIN;
    if (outcome == AZ_COMMAND_APPLIED) apply(&submitted);
    result(out, rid, outcome);
    return AZ_OK;
}
static void tick(void) {
    virtual_now += UINT64_C(250000000);
    probe_poll();
    probe_plan_step();
}
static void settle_plan(void) {
    for (unsigned i = 0; i < 800 && (probe.plan.kind || probe.pending); i++) tick();
    assert(!probe.plan.kind && !probe.pending);
}
static struct probe_input plan_input(const char *kind, uint64_t first) {
    struct probe_input v = {.pid = (uint64_t) getpid(), .id = first, .load = !strcmp(kind, "refusals") ? UINT64_MAX : 42, .deadline = virtual_now + UINT64_C(10000000000), .deck = 2};
    strcpy(v.action, kind);
    return v;
}
static void begin_case(void) {
    strcpy(directory, "/tmp/sdk-loop-batch.XXXXXX");
    assert(mkdtemp(directory));
    reset();
    library.command_submit = batch_submit;
    library.command_poll = batch_poll;
    library.deck_snapshot = batch_snapshot;
    actual_load = 42;
    applied_id = 0;
    call_count = 0;
    delay_polls = remaining_polls = observer_delay = observer_left = fail_at = 0;
    immediate = unexpected_refusal = 0;
    actual_state = (struct model_state) {1, 1, 3, 1, virtual_now};
    public_state = actual_state;
    memset(operations, 0, sizeof operations);
    memset(submit_times, 0, sizeof submit_times);
    memset(applied_times, 0, sizeof applied_times);
}
static void end_case(void) {
    char path[1100];
    snprintf(path, sizeof path, "%s/results.%ld.log", directory, (long) getpid());
    assert(!unlink(path));
    assert(!rmdir(directory));
}
static void done_status(const char *status, unsigned completed) {
    char expected[96];
    snprintf(expected, sizeof expected, "status=%s completed=%u", status, completed);
    assert(strstr(last_log, "SDK_LOOP_PLAN v=1 event=done") && strstr(last_log, expected));
}
int main(void) {
    static const unsigned matrix[15] = {4, 1, 2, 7, 8, 9, 11, 12, 10, 4, 5, 4, 3, 4, 6};
    for (unsigned deferred = 0; deferred < 2; deferred++) {
        begin_case();
        immediate = !deferred;
        delay_polls = deferred ? 2 : 0;
        observer_delay = deferred ? 6 : 0;
        struct probe_input v = plan_input("matrix", 20);
        assert(probe_handle(&v) == AZ_OK && !call_count);
        struct probe_input snapshot_input = {.pid = (uint64_t) getpid(), .deck = 2};
        strcpy(snapshot_input.action, "snapshot");
        assert(probe_handle(&snapshot_input) == AZ_OK);
        struct probe_input ordinary = input(100);
        assert(probe_handle(&ordinary) == AZ_BUSY && !call_count);
        settle_plan();
        assert(call_count == 15 && !memcmp(operations, matrix, sizeof matrix));
        done_status("PASS", 15);
        assert(submit_times[2] >= applied_times[1] + UINT64_C(1000000000));
        v.deadline = virtual_now + UINT64_C(10000000000);
        assert(probe.highwater == 34 && probe_handle(&v) == AZ_STALE);
        end_case();
    }
    begin_case();
    struct probe_input v = plan_input("refusals", 50);
    assert(probe_handle(&v) == AZ_OK);
    settle_plan();
    assert(call_count == 12);
    for (unsigned i = 0; i < 12; i++) assert(operations[i] == i + 1);
    done_status("PASS", 12);
    end_case();
    begin_case();
    unexpected_refusal = 1;
    v = plan_input("refusals", 80);
    assert(probe_handle(&v) == AZ_OK);
    settle_plan();
    assert(call_count == 1);
    done_status("FAIL", 0);
    end_case();
    begin_case();
    fail_at = 4;
    v = plan_input("matrix", 100);
    assert(probe_handle(&v) == AZ_OK);
    settle_plan();
    assert(call_count == 4);
    done_status("FAIL", 3);
    end_case();
    begin_case();
    v = plan_input("matrix", 130);
    assert(probe_handle(&v) == AZ_OK);
    tick();
    assert(call_count == 1);
    actual_load = 43;
    settle_plan();
    assert(call_count == 1);
    done_status("INCOMPLETE", 0);
    end_case();
    begin_case();
    v = plan_input("matrix", 160);
    assert(probe_handle(&v) == AZ_OK);
    tick();
    assert(probe.pending);
    struct probe_input c = {.pid = (uint64_t) getpid(), .id = 160, .deck = 2};
    strcpy(c.action, "cancel");
    assert(probe_handle(&c) == AZ_OK);
    settle_plan();
    assert(call_count == 1);
    done_status("INCOMPLETE", 0);
    end_case();
    begin_case();
    v = plan_input("matrix", 190);
    assert(probe_handle(&v) == AZ_OK);
    tick();
    virtual_now = probe.plan.overall_deadline;
    probe_plan_step();
    assert(!probe.plan.kind && probe.pending);
    settle_plan();
    assert(call_count == 1);
    end_case();
    begin_case();
    observer_delay = 10000;
    v = plan_input("matrix", 220);
    assert(probe_handle(&v) == AZ_OK);
    settle_plan();
    assert(call_count == 3);
    done_status("INCOMPLETE", 2);
    end_case();
    begin_case();
    v = plan_input("matrix", 250);
    v.pid++;
    assert(probe_handle(&v) == AZ_STALE && !call_count && !probe.plan.kind);
    v = plan_input("matrix", UINT64_MAX - 14);
    assert(probe_handle(&v) == AZ_INVALID);
    v = plan_input("refusals", 280);
    v.load = 42;
    assert(probe_handle(&v) == AZ_INVALID);
    v = plan_input("matrix", 300);
    v.deadline = virtual_now - 1;
    assert(probe_handle(&v) == AZ_INVALID);
    end_case();
    puts("PASS explicit loop batches: full immediate/deferred matrix, stale refusals, observer lag, no replay, cancellation and stop-on-failure");
    return 0;
}
