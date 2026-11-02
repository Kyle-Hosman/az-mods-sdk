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
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define ID "sdk-loop-command-probe"
#ifndef PROBE_MAX_TICKS
#define PROBE_MAX_TICKS 36000u
#endif
#define PROBE_MAX_RECORDS 4096u
struct probe_input {
    uint64_t pid, id, load, deadline;
    uint32_t deck, op;
    double position, end, beats;
    char action[9];
};
enum {
    PLAN_NONE,
    PLAN_REFUSALS,
    PLAN_MATRIX
};
struct probe_plan {
    int kind, phase, stopping;
    unsigned index, count, completed;
    uint64_t pid, first_id, load, overall_deadline, next_at, step_started, observe_deadline;
    uint64_t before_unit, before_revision;
    struct probe_input current;
};
struct probe_state {
    const az_core_v1 *core;
    const az_library_v1 *library;
    uint64_t highwater, sequence, commands;
    struct probe_input active;
    az_command_result last;
    int pending, terminal, last_rc, io_failed;
    struct probe_plan plan;
};
static struct probe_state probe;
static uint64_t probe_now(void) {
#ifdef PROBE_TEST_NOW
    return PROBE_TEST_NOW();
#else
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) || ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L) return 0;
    uint64_t sec = (uint64_t) ts.tv_sec;
    return sec <= (UINT64_MAX - (uint64_t) ts.tv_nsec) / UINT64_C(1000000000)
               ? sec * UINT64_C(1000000000) + (uint64_t) ts.tv_nsec
               : 0;
#endif
}
static int probe_integer(const char *s, uint64_t *out) {
    if (!*s || (s[0] == '0' && s[1])) return 0;
    uint64_t value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || value > (UINT64_MAX - (unsigned) (*s - '0')) / 10) return 0;
        value = value * 10 + (unsigned) (*s - '0');
    }
    *out = value;
    return 1;
}
static int probe_number(const char *s, double *out) {
    /* No whitespace, hexadecimal, NaN or infinity spellings in the transport. */
    if (!*s || strspn(s, "0123456789.eE+-") != strlen(s)) return 0;
    char *end;
    errno = 0;
    double value = strtod(s, &end);
    if (*end || errno || !isfinite(value)) return 0;
    *out = value;
    return 1;
}
static int probe_parse(const char *line, size_t n, struct probe_input *out) {
    static const char *keys[] = {"v=", "pid=", "action=", "id=", "deck=", "op=", "load=", "deadline_ns=", "position=", "end=", "beats="};
    if (!line || !out || !n || n > 511 || line[n - 1] != '\n' || memchr(line, 0, n)) return 0;
    char copy[512];
    memcpy(copy, line, n - 1);
    copy[n - 1] = 0;
    char *value[11], *at = copy;
    for (unsigned i = 0; i < 11; ++i) {
        size_t key = strlen(keys[i]);
        if (strncmp(at, keys[i], key)) return 0;
        value[i] = at + key;
        char *space = strchr(value[i], ' ');
        if (i != 10) {
            if (!space || space == value[i]) return 0;
            *space = 0;
            at = space + 1;
        } else if (space) return 0;
    }
    struct probe_input v = {0};
    uint64_t version, deck, op;
    if (!probe_integer(value[0], &version) || version != 1 || !probe_integer(value[1], &v.pid) ||
        !probe_integer(value[3], &v.id) || !probe_integer(value[4], &deck) || deck > 3 ||
        !probe_integer(value[5], &op) || op > 12 || !probe_integer(value[6], &v.load) ||
        !probe_integer(value[7], &v.deadline) || !probe_number(value[8], &v.position) ||
        !probe_number(value[9], &v.end) || !probe_number(value[10], &v.beats)) return 0;
    if (strcmp(value[2], "submit") && strcmp(value[2], "cancel") && strcmp(value[2], "snapshot") && strcmp(value[2], "refusals") && strcmp(value[2], "matrix")) return 0;
    v.deck = (uint32_t) deck;
    v.op = (uint32_t) op;
    memcpy(v.action, value[2], strlen(value[2]) + 1);
    *out = v;
    return 1;
}
static int probe_results_path(char *path, size_t capacity) {
    const char *directory = probe.core->data_dir(ID);
    if (!directory || directory[0] != '/' || strlen(directory) > 300 || strpbrk(directory, " \t\r\n")) return 0;
    int n = snprintf(path, capacity, "%s/results.%ld.log", directory, (long) getpid());
    return n > 0 && (size_t) n < capacity;
}
static void probe_write_record(const char *line, size_t length) {
    char path[512];
    int fd = -1;
    struct stat st;
    int okay = probe_results_path(path, sizeof path);
    if (okay) fd = open(path, O_WRONLY | O_APPEND | O_NOFOLLOW | O_NONBLOCK);
    okay = okay && fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size >= 0 &&
           length <= 1024 && st.st_size <= (off_t) (2u * 1024u * 1024u - length);
    if (okay) okay = write(fd, line, length) == (ssize_t) length;
    if (fd >= 0 && close(fd)) okay = 0;
    if (!okay && !probe.io_failed) {
        probe.io_failed = 1;
        probe.core->log(ID, "SDK_LOOP_IO error=results_file records=%llu", (unsigned long long) probe.sequence);
    }
}
static void probe_emit(const char *event, const struct probe_input *input, int rc, const az_command_result *result) {
    if (probe.sequence >= PROBE_MAX_RECORDS) return;
    az_deck_snapshot snapshot = {.size = sizeof snapshot};
    int src = probe.library->deck_snapshot((int) input->deck, &snapshot);
    uint64_t query = probe_now();
    int valid = src == AZ_OK && snapshot.size >= sizeof snapshot && snapshot.version == AZ_DATA_VERSION && snapshot.deck == input->deck;
    int load_valid = valid && (snapshot.valid & AZ_LIVE_NATIVE_LOAD) && snapshot.native_load_generation &&
                     snapshot.native_load_unit_generation && snapshot.native_load_observed_ns && compare_timestamps(snapshot.native_load_observed_ns, query, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL;
    int engaged_valid = valid && (snapshot.valid & AZ_LIVE_LOOP_ENGAGED) && (snapshot.loop.valid & AZ_LOOP_ENGAGED) &&
                        (snapshot.loop.engaged == 0 || snapshot.loop.engaged == 1) && snapshot.loop_unit_generation && snapshot.loop_revision && snapshot.loop_observed_ns && compare_timestamps(snapshot.loop_observed_ns, query, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL;
    int bounds_valid = valid && (snapshot.valid & AZ_LIVE_LOOP_BOUNDS) && (snapshot.loop.valid & AZ_LOOP_BOUNDS) &&
                       snapshot.loop_unit_generation && snapshot.loop_revision && snapshot.loop_observed_ns && compare_timestamps(snapshot.loop_observed_ns, query, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL && isfinite(snapshot.loop.in_s) &&
                       isfinite(snapshot.loop.out_s);
    /* Collect source coordinates without inventing their sample rate. The
     * controller validates their order using its bound source rate. */
    char line[1024];
    int length = snprintf(line, sizeof line, "SDK_LOOP v=1 seq=%llu event=%s id=%llu deck=%u op=%u target_load=%llu rc=%d outcome=%d reason=%d revision=%llu receipt_ns=%llu query_ns=%llu load_valid=%d native_load=%llu load_unit=%llu load_ns=%llu engaged_valid=%d engaged=%d bounds_valid=%d in=%.17g out=%.17g loop_unit=%llu loop_rev=%llu loop_ns=%llu\n",
                          (unsigned long long) ++probe.sequence, event, (unsigned long long) input->id, input->deck, input->op,
                          (unsigned long long) input->load, rc, result ? (int) result->outcome : -1, result ? result->reason : 0,
                          (unsigned long long) (result ? result->revision : 0), (unsigned long long) (result ? result->observed_ns : 0),
                          (unsigned long long) query, !!load_valid, (unsigned long long) (load_valid ? snapshot.native_load_generation : 0),
                          (unsigned long long) (load_valid ? snapshot.native_load_unit_generation : 0),
                          (unsigned long long) (load_valid ? snapshot.native_load_observed_ns : 0), !!engaged_valid,
                          engaged_valid ? snapshot.loop.engaged : -1, !!bounds_valid, bounds_valid ? snapshot.loop.in_s : 0, bounds_valid ? snapshot.loop.out_s : 0,
                          (unsigned long long) (engaged_valid || bounds_valid ? snapshot.loop_unit_generation : 0),
                          (unsigned long long) (engaged_valid || bounds_valid ? snapshot.loop_revision : 0),
                          (unsigned long long) (engaged_valid || bounds_valid ? snapshot.loop_observed_ns : 0));
    if (length > 0 && (size_t) length < sizeof line) probe_write_record(line, (size_t) length);
    else if (!probe.io_failed) {
        probe.io_failed = 1;
        probe.core->log(ID, "SDK_LOOP_IO error=format");
    }
}
static int probe_result_valid(const az_command_result *value, uint64_t id) {
    return value->size >= sizeof *value && value->version == AZ_DATA_VERSION &&
           value->request_id == id && value->outcome <= AZ_COMMAND_UNCERTAIN;
}
static void probe_poll(void) {
    if (!probe.pending) return;
    if (probe.terminal) {
        int rc = probe.library->command_release(ID, probe.active.id);
        if (rc != AZ_BUSY) probe_emit("release", &probe.active, rc, &probe.last);
        if (rc == AZ_OK) {
            probe.pending = probe.terminal = 0;
            memset(&probe.active, 0, sizeof probe.active);
        }
        return;
    }
    az_command_result value = {.size = sizeof value};
    int rc = probe.library->command_poll(ID, probe.active.id, &value);
    if (rc == AZ_OK && probe_result_valid(&value, probe.active.id)) {
        if (memcmp(&value, &probe.last, sizeof value) || probe.last_rc != rc) probe_emit("poll", &probe.active, rc, &value);
        probe.last = value;
        if (value.outcome != AZ_COMMAND_ACCEPTED) probe.terminal = 1;
    } else if (rc != AZ_BUSY && rc != probe.last_rc) probe_emit("poll", &probe.active, rc, NULL);
    probe.last_rc = rc;
}
static int probe_submit(const struct probe_input *v) {
    az_data_capabilities caps = {.size = sizeof caps};
    int rc = probe.library->data_capabilities(&caps);
    if (rc != AZ_OK || !(caps.commands & (UINT64_C(1) << v->op))) {
        probe_emit("refused", v, AZ_UNSUPPORTED, NULL);
        return AZ_UNSUPPORTED;
    }
    az_command_request request = {.size = sizeof request, .version = AZ_DATA_VERSION, .operation = v->op, .deck = v->deck, .request_id = v->id, .expected_load_generation = v->load, .deadline_ns = v->deadline, .quantization = AZ_QUANTIZE_NATIVE, .flags = AZ_COMMAND_NATIVE_LOAD, .position_s = v->position, .end_s = v->end, .beats = v->beats};
    az_command_result result = {.size = sizeof result};
    memset(&probe.last, 0, sizeof probe.last);
    rc = probe.library->command_submit(ID, &request, &result);
    probe_emit("submit", v, rc, (rc == AZ_OK || rc == AZ_PENDING || rc == AZ_STALE || rc == AZ_UNSUPPORTED) && probe_result_valid(&result, v->id) ? &result : NULL);
    if (probe_result_valid(&result, v->id)) probe.last = result;
    if (rc == AZ_OK && probe_result_valid(&result, v->id)) {
        probe.active = *v;
        probe.last = result;
        probe.pending = 1;
        probe.terminal = result.outcome != AZ_COMMAND_ACCEPTED;
        probe.last_rc = AZ_OK;
    }
    return rc;
}
static int probe_plan_start(const struct probe_input *v);
static int probe_handle(const struct probe_input *v) {
    uint64_t now = probe_now();
    if (v->pid != (uint64_t) getpid() || !now) {
        probe_emit("refused", v, AZ_STALE, NULL);
        return AZ_STALE;
    }
    if (!strcmp(v->action, "snapshot")) {
        if (v->id || v->op || v->load || v->deadline || v->position || v->end || v->beats) {
            probe_emit("refused", v, AZ_INVALID, NULL);
            return AZ_INVALID;
        }
        probe_emit("snapshot", v, AZ_OK, NULL);
        return AZ_OK;
    }
    if (!strcmp(v->action, "cancel")) {
        if (!probe.pending || v->id != probe.active.id || v->deck != probe.active.deck || v->op || v->load || v->deadline || v->position || v->end || v->beats) {
            probe_emit("refused", v, AZ_INVALID, NULL);
            return AZ_INVALID;
        }
        if (probe.plan.kind) probe.plan.stopping = 1;
        az_command_result result = {.size = sizeof result};
        int rc = probe.library->command_cancel(ID, v->id, &result);
        probe_emit("cancel", &probe.active, rc, (rc == AZ_OK || rc == AZ_PENDING || rc == AZ_STALE) && probe_result_valid(&result, v->id) ? &result : NULL);
        return rc;
    }
    if (!strcmp(v->action, "refusals") || !strcmp(v->action, "matrix")) return probe_plan_start(v);
    if (probe.plan.kind) {
        probe_emit("refused", v, AZ_BUSY, NULL);
        return AZ_BUSY;
    }
    if (strcmp(v->action, "submit") || !v->id || !v->op || !v->load || compare_timestamps(v->deadline, now, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL || compare_timestamps(v->deadline - now, UINT64_C(30000000000), 1000000000, 1000000000, 1) == AZ_TIME_AFTER) {
        probe_emit("refused", v, AZ_INVALID, NULL);
        return AZ_INVALID;
    }
    if (v->id <= probe.highwater) {
        probe_emit("refused", v, AZ_STALE, NULL);
        return AZ_STALE;
    }
    probe.highwater = v->id;
    if (probe.pending) {
        probe_emit("refused", v, AZ_BUSY, NULL);
        return AZ_BUSY;
    }
    return probe_submit(v);
}


static void probe_plan_record(const char *event, const char *status) {
    char line[512];
    int n = snprintf(line, sizeof line,
                     "SDK_LOOP_PLAN v=1 event=%s kind=%s id=%llu deck=%u load=%llu status=%s completed=%u query_ns=%llu deadline_ns=%llu\n",
                     event, probe.plan.kind == PLAN_REFUSALS ? "refusals" : "matrix",
                     (unsigned long long) probe.plan.first_id, probe.plan.current.deck, (unsigned long long) probe.plan.load,
                     status, probe.plan.completed, (unsigned long long) probe_now(), (unsigned long long) probe.plan.overall_deadline);
    if (n > 0 && (size_t) n < sizeof line) probe_write_record(line, (size_t) n);
}
static void probe_plan_finish(const char *status) {
    if (!probe.plan.kind) return;
    probe_plan_record("done", status);
    probe.plan.kind = PLAN_NONE;
}
static int probe_snapshot(uint32_t deck, az_deck_snapshot *snapshot, uint64_t now) {
    memset(snapshot, 0, sizeof *snapshot);
    snapshot->size = sizeof *snapshot;
    int rc = probe.library->deck_snapshot((int) deck, snapshot);
    uint64_t query = probe_now();
    return query >= now && rc == AZ_OK && snapshot->size >= sizeof *snapshot &&
           snapshot->version == AZ_DATA_VERSION && snapshot->deck == deck && (snapshot->valid & AZ_LIVE_NATIVE_LOAD) &&
           snapshot->native_load_generation && snapshot->native_load_unit_generation && snapshot->native_load_observed_ns &&
           compare_timestamps(snapshot->native_load_observed_ns, query, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL;
}
static void probe_plan_snapshot(void) {
    struct probe_input snapshot = {.deck = probe.plan.current.deck};
    probe_emit("snapshot", &snapshot, AZ_OK, NULL);
}
static void probe_plan_cancel(void) {
    if (probe.pending && !probe.terminal) {
        az_command_result result = {.size = sizeof result};
        int rc = probe.library->command_cancel(ID, probe.active.id, &result);
        probe_emit("cancel", &probe.active, rc, probe_result_valid(&result, probe.active.id) ? &result : NULL);
    }
}
static int probe_plan_start(const struct probe_input *v) {
    uint64_t now = probe_now();
    int kind = !strcmp(v->action, "refusals") ? PLAN_REFUSALS : PLAN_MATRIX;
    unsigned count = kind == PLAN_REFUSALS ? 12u : 15u;
    if (probe.plan.kind || probe.pending) {
        probe_emit("refused", v, AZ_BUSY, NULL);
        return AZ_BUSY;
    }
    if (!v->id || v->id > UINT64_MAX - 15 || v->deck >= 4 || v->op || !v->load || v->position || v->end || v->beats ||
        compare_timestamps(v->deadline, now, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL || compare_timestamps(v->deadline - now, UINT64_C(30000000000), 1000000000, 1000000000, 1) == AZ_TIME_AFTER ||
        (kind == PLAN_REFUSALS && v->load != UINT64_MAX) || now > UINT64_MAX - UINT64_C(180000000000)) {
        probe_emit("refused", v, AZ_INVALID, NULL);
        return AZ_INVALID;
    }
    if (v->id <= probe.highwater) {
        probe_emit("refused", v, AZ_STALE, NULL);
        return AZ_STALE;
    }
    probe.highwater = v->id + count - 1; /* Never reuse even unissued reserved plan IDs. */
    memset(&probe.plan, 0, sizeof probe.plan);
    probe.plan.kind = kind;
    probe.plan.phase = 1;
    probe.plan.count = count;
    probe.plan.pid = v->pid;
    probe.plan.first_id = v->id;
    probe.plan.load = v->load;
    probe.plan.current = *v;
    probe.plan.overall_deadline = now + UINT64_C(180000000000);
    probe_plan_record("start", "INCOMPLETE");
    return AZ_OK;
}
static void probe_plan_arguments(struct probe_input *v) {
    static const unsigned matrix[15] = {AZ_CMD_LOOP_EXIT, AZ_CMD_LOOP_IN, AZ_CMD_LOOP_OUT, AZ_CMD_LOOP_DOUBLE,
                                        AZ_CMD_LOOP_HALVE, AZ_CMD_LOOP_LENGTH, AZ_CMD_LOOP_ADJUST_IN, AZ_CMD_LOOP_ADJUST_OUT, AZ_CMD_LOOP_MOVE,
                                        AZ_CMD_LOOP_EXIT, AZ_CMD_RELOOP, AZ_CMD_LOOP_EXIT, AZ_CMD_LOOP_ENTER, AZ_CMD_LOOP_EXIT, AZ_CMD_LOOP_CLEAR};
    memset(v, 0, sizeof *v);
    v->pid = probe.plan.pid;
    v->id = probe.plan.first_id + probe.plan.index;
    v->deck = probe.plan.current.deck;
    v->load = probe.plan.load;
    v->op = probe.plan.kind == PLAN_REFUSALS ? probe.plan.index + 1 : matrix[probe.plan.index];
    strcpy(v->action, "submit");
    if (v->op == AZ_CMD_LOOP_LENGTH) v->beats = 8;
    if (v->op == AZ_CMD_LOOP_MOVE || v->op == AZ_CMD_LOOP_ADJUST_IN) v->position = .05;
    if (v->op == AZ_CMD_LOOP_ADJUST_OUT) v->end = .05;
}
static void probe_plan_advance(uint64_t now) {
    unsigned operation = probe.plan.current.op;
    probe.plan.completed++;
    probe.plan.index++;
    if (probe.plan.index == probe.plan.count) {
        probe_plan_finish("PASS");
        return;
    }
    probe.plan.phase = 1;
    probe.plan.next_at = now + (operation == AZ_CMD_LOOP_IN && probe.plan.kind == PLAN_MATRIX ? UINT64_C(1000000000) : 0);
}
static void probe_plan_step(void) {
    if (!probe.plan.kind) return;
    uint64_t now = probe_now();
    if (probe.io_failed) {
        probe.plan.kind = PLAN_NONE;
        return;
    }
    if (!now || compare_timestamps(now, probe.plan.overall_deadline, 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL || probe.plan.pid != (uint64_t) getpid()) {
        probe_plan_cancel();
        probe_plan_finish("INCOMPLETE");
        return;
    }
    if (probe.plan.stopping && !probe.pending) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    if (probe.plan.phase == 2) {
        if (probe.pending) return;
        if (probe.plan.stopping) {
            probe_plan_finish("INCOMPLETE");
            return;
        }
        /* Any retained request during refusal testing is unexpected, even if
         * subsequently cancelled or applied. Drain it without issuing later work. */
        if (probe.plan.kind == PLAN_REFUSALS || !probe_result_valid(&probe.last, probe.plan.current.id) ||
            probe.last.outcome != AZ_COMMAND_APPLIED) {
            probe_plan_finish("FAIL");
            return;
        }
        probe.plan.phase = 3;
        probe.plan.observe_deadline = now + UINT64_C(5000000000);
    }
    if (probe.plan.phase == 3) {
        az_deck_snapshot snapshot;
        int loaded = probe_snapshot(probe.plan.current.deck, &snapshot, now);
        probe_plan_snapshot();
        if (!loaded || snapshot.native_load_generation != probe.plan.load) {
            probe_plan_finish("INCOMPLETE");
            return;
        }
        unsigned op = probe.plan.current.op;
        if (op == AZ_CMD_LOOP_IN || op == AZ_CMD_LOOP_CLEAR || probe.plan.index == 0) {
            probe_plan_advance(now);
            return;
        }
        int visible = (snapshot.valid & AZ_LIVE_LOOP_ENGAGED) && (snapshot.loop.valid & AZ_LOOP_ENGAGED) &&
                      snapshot.loop_unit_generation && snapshot.loop_revision > probe.plan.before_revision &&
                      (!probe.plan.before_unit || snapshot.loop_unit_generation == probe.plan.before_unit) &&
                      compare_timestamps(snapshot.loop_observed_ns, probe.plan.step_started, 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL && compare_timestamps(snapshot.loop_observed_ns, probe_now(), 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL;
        if (op == AZ_CMD_LOOP_EXIT) visible = visible && snapshot.loop.engaged == 0;
        else visible = visible && snapshot.loop.engaged == 1 && (snapshot.valid & AZ_LIVE_LOOP_BOUNDS) &&
                       (snapshot.loop.valid & AZ_LOOP_BOUNDS) && isfinite(snapshot.loop.in_s) && isfinite(snapshot.loop.out_s);
        if (visible) {
            probe_plan_advance(now);
            return;
        }
        if (compare_timestamps(now, probe.plan.observe_deadline, 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL) probe_plan_finish("INCOMPLETE");
        return;
    }
    if (probe.plan.phase != 1 || compare_timestamps(now, probe.plan.next_at, 1000000000, 1000000000, 1) == AZ_TIME_BEFORE || probe.pending) return;
    if (probe.plan.stopping) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    struct probe_input request;
    probe_plan_arguments(&request);
    az_deck_snapshot before;
    int loaded = probe_snapshot(request.deck, &before, now);
    probe_plan_snapshot();
    if (probe.plan.kind == PLAN_MATRIX && (!loaded || before.native_load_generation != probe.plan.load)) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    probe.plan.before_unit = before.loop_unit_generation;
    probe.plan.before_revision = before.loop_revision;
    uint64_t dispatch = probe_now();
    if (!dispatch || compare_timestamps(dispatch, probe.plan.overall_deadline, 1000000000, 1000000000, 1) >= AZ_TIME_EQUAL || dispatch > UINT64_MAX - UINT64_C(10000000000)) {
        probe_plan_finish("INCOMPLETE");
        return;
    }
    probe.plan.step_started = dispatch;
    request.deadline = dispatch + UINT64_C(10000000000);
    probe.plan.current = request;
    int rc = probe_submit(&request);
    if (probe.io_failed) {
        probe.plan.kind = PLAN_NONE;
        return;
    }
    if (probe.plan.kind == PLAN_REFUSALS && rc == AZ_STALE && probe_result_valid(&probe.last, request.id) &&
        probe.last.outcome == AZ_COMMAND_REJECTED && probe.last.reason == AZ_STALE && !probe.last.revision && !probe.pending) {
        probe_plan_snapshot();
        probe_plan_advance(now);
        return;
    }
    if (probe.pending) {
        probe.plan.phase = 2;
        return;
    }
    probe_plan_finish("FAIL");
}

static void probe_ready(const char *directory) {
    probe.core->log(ID, "SDK_LOOP_READY v=1 pid=%ld commands=%llu data=%s", (long) getpid(), (unsigned long long) probe.commands, directory);
}
static void probe_step(void) {
    probe_poll();
    probe_plan_step();
    if (probe.io_failed) return;
    const char *directory = probe.core->data_dir(ID);
    if (!directory || directory[0] != '/' || strlen(directory) > 300 || strpbrk(directory, " \t\r\n")) return;
    az_data_capabilities caps = {.size = sizeof caps};
    if (probe.library->data_capabilities(&caps) == AZ_OK && caps.version == AZ_DATA_VERSION && caps.commands != probe.commands) {
        probe.commands = caps.commands;
        probe_ready(directory);
    }
    char request[1024], claimed[1024];
    int a = snprintf(request, sizeof request, "%s/request", directory), b = snprintf(claimed, sizeof claimed, "%s/request.processing", directory);
    if (a < 0 || (size_t) a >= sizeof request || b < 0 || (size_t) b >= sizeof claimed || rename(request, claimed)) return;
    int fd = open(claimed, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    char line[512];
    ssize_t n = -1;
    if (fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < (off_t) sizeof line)
        n = read(fd, line, sizeof line);
    if (fd >= 0) close(fd);
    unlink(claimed);
    struct probe_input v = {0};
    if (n <= 0 || !probe_parse(line, (size_t) n, &v)) probe_emit("refused", &v, AZ_INVALID, NULL);
    else (void) probe_handle(&v);
}
static void *probe_worker(void *unused) {
    (void) unused;
    for (unsigned tick = 0; tick < PROBE_MAX_TICKS && probe.sequence < PROBE_MAX_RECORDS && (!probe.io_failed || probe.pending); ++tick) {
        probe_step();
        struct timespec delay = {0, 50000000};
        if (nanosleep(&delay, NULL) && errno != EINTR) break;
    }
    if (probe.plan.kind) {
        probe_plan_cancel();
        probe_plan_finish("INCOMPLETE");
    }
    probe.core->log(ID, "SDK_LOOP_DONE records=%llu pending=%d", (unsigned long long) probe.sequence, probe.pending);
    return NULL;
}
static int init(const az_core_v1 *core) {
    if (!core || core->abi != AZ_MOD_ABI || !AZ_HAS(core, log) || !AZ_HAS(core, data_dir) ||
        !AZ_HAS(core, thread_start) || !AZ_HAS(core, library_service)) return -1;
    const az_library_v1 *lib = core->library_service(AZ_LIBRARY_ABI);
    if (!AZ_LIB_HAS(lib, data_capabilities) || !AZ_LIB_HAS(lib, deck_snapshot) || !AZ_LIB_HAS(lib, command_submit) ||
        !AZ_LIB_HAS(lib, command_poll) || !AZ_LIB_HAS(lib, command_cancel) || !AZ_LIB_HAS(lib, command_release)) return -1;
    az_data_capabilities caps = {.size = sizeof caps};
    if (lib->data_capabilities(&caps) != AZ_OK) return -1;
    memset(&probe, 0, sizeof probe);
    probe.core = core;
    probe.library = lib;
    probe.commands = caps.commands;
    const char *directory = core->data_dir(ID);
    if (!directory || directory[0] != '/' || strlen(directory) > 300 || strpbrk(directory, " \t\r\n")) return -1;
    char path[512];
    if (!probe_results_path(path, sizeof path)) return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK, 0600);
    if (fd < 0) return -1;
    struct stat st;
    int okay = !fstat(fd, &st) && S_ISREG(st.st_mode);
    if (close(fd)) okay = 0;
    if (!okay) return -1;
    probe_ready(directory);
    return core->thread_start(ID, "sdk-loop-command", probe_worker, NULL) == 0 ? 0 : -1;
}
static const char *const tags[] = {"any", NULL};
static const az_mod_v1 mod = {.abi = AZ_MOD_ABI, .size = sizeof mod, .id = ID, .fw_tags = tags, .init = init};
AZ_MOD_EXPORT const az_mod_v1 *az_mod_v1_entry(void) { return &mod; }
