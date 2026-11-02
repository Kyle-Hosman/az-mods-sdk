/* SPDX-License-Identifier: GPL-3.0-only */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#ifndef LOOP_PROBE_IMPL
#define LOOP_PROBE_IMPL "../../examples/loop-command-probe/probe.c"
#endif
#include LOOP_PROBE_IMPL
static az_library_v1 library;
static az_core_v1 core;
static char directory[1024], last_log[2048];
static unsigned submits, polls, releases, cancels, threads, snapshots;
static uint64_t mask = 8190;
static int terminal_outcome = AZ_COMMAND_APPLIED, submit_outcome = AZ_COMMAND_ACCEPTED, release_busy, request_supported = 1, bad_result;
static az_command_request submitted;
static int long_bounds;
static void log_line(const char *id, const char *format, ...) {
    assert(!strcmp(id, ID));
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(last_log, sizeof last_log, format, ap);
    va_end(ap);
    assert(n > 0 && (size_t) n < sizeof last_log);
}
static const char *last_record(void) {
    char path[1100];
    snprintf(path, sizeof path, "%s/results.%ld.log", directory, (long) getpid());
    FILE *f = fopen(path, "r");
    if (!f) return last_log;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        assert(strchr(line, '\n'));
        strcpy(last_log, line);
    }
    fclose(f);
    return last_log;
}
#define last_log last_record()
static const char *data_directory(const char *id) {
    assert(!strcmp(id, ID));
    return directory;
}
static int capabilities(az_data_capabilities *out) {
    assert(out && out->size == sizeof *out);
    out->version = AZ_DATA_VERSION;
    out->commands = mask;
    return AZ_OK;
}
static int snapshot(int deck, az_deck_snapshot *out) {
    assert(deck >= 0 && deck < 4 && out->size == sizeof *out);
    snapshots++;
    uint64_t stamp = probe_now();
    out->version = AZ_DATA_VERSION;
    out->deck = (unsigned) deck;
    out->valid = AZ_LIVE_NATIVE_LOAD | AZ_LIVE_LOOP_ENGAGED | AZ_LIVE_LOOP_BOUNDS;
    out->native_load_generation = 42;
    out->native_load_unit_generation = 11;
    out->native_load_observed_ns = stamp;
    out->loop.valid = AZ_LOOP_ENGAGED | AZ_LOOP_BOUNDS;
    out->loop.engaged = 1;
    out->loop.in_s = long_bounds ? 1.1234567891234567 : 1.0;
    out->loop.out_s = long_bounds ? 3.9876543219876543 : 3.0;
    out->loop_observed_ns = stamp;
    out->loop_unit_generation = 11;
    out->loop_revision = 5;
    return AZ_OK;
}
static void result(az_command_result *out, uint64_t id, int outcome) {
    *out = (az_command_result) {.size = sizeof *out, .version = AZ_DATA_VERSION, .request_id = id, .outcome = (unsigned) outcome, .reason = outcome == AZ_COMMAND_ACCEPTED ? AZ_PENDING : outcome == AZ_COMMAND_APPLIED ? AZ_OK
                                                                                                                                                                                      : outcome == AZ_COMMAND_CANCELLED ? AZ_CANCELLED
                                                                                                                                                                                                                        : AZ_FAILED,
                                .revision = outcome == AZ_COMMAND_APPLIED ? 7 : 0,
                                .observed_ns = probe_now()};
}
static int submit(const char *id, const az_command_request *r, az_command_result *out) {
    assert(!strcmp(id, ID));
    submits++;
    submitted = *r;
    assert(r->flags == AZ_COMMAND_NATIVE_LOAD && r->quantization == AZ_QUANTIZE_NATIVE && !r->expected_revision);
    if (!request_supported) return AZ_INVALID;
    if (r->expected_load_generation != 42) {
        result(out, r->request_id, AZ_COMMAND_REJECTED);
        out->reason = AZ_STALE;
        return AZ_STALE;
    }
    result(out, r->request_id, submit_outcome);
    if (bad_result == 1) out->size = offsetof(az_command_result, revision);
    if (bad_result == 2) out->version = 99;
    if (bad_result == 3) out->request_id++;
    return AZ_OK;
}
static int poll(const char *id, uint64_t rid, az_command_result *out) {
    assert(!strcmp(id, ID) && rid == submitted.request_id);
    polls++;
    result(out, rid, terminal_outcome);
    return AZ_OK;
}
static int cancel(const char *id, uint64_t rid, az_command_result *out) {
    assert(!strcmp(id, ID) && rid == submitted.request_id);
    cancels++;
    terminal_outcome = AZ_COMMAND_CANCELLED;
    result(out, rid, AZ_COMMAND_CANCELLED);
    return AZ_OK;
}
static int release_result(const char *id, uint64_t rid) {
    assert(!strcmp(id, ID) && rid == submitted.request_id);
    releases++;
    if (release_busy) {
        release_busy = 0;
        return AZ_BUSY;
    }
    return AZ_OK;
}
static const az_library_v1 *service(unsigned version) {
    assert(version == 1);
    return &library;
}
static int start(const char *id, const char *name, void *(*worker)(void *), void *arg) {
    assert(!strcmp(id, ID) && !strcmp(name, "sdk-loop-command") && worker == probe_worker && !arg);
    threads++;
    return 0;
}
static struct probe_input input(uint64_t id) {
    struct probe_input v = {.pid = (uint64_t) getpid(), .id = id, .load = 42, .deadline = probe_now() + 10000000000ULL, .deck = 0, .op = AZ_CMD_LOOP_DOUBLE};
    strcpy(v.action, "submit");
    return v;
}
static void reset(void) {
    mask = 8190;
    terminal_outcome = AZ_COMMAND_APPLIED;
    release_busy = 0;
    request_supported = 1;
    library = (az_library_v1) {.abi = 1, .size = sizeof library, .data_capabilities = capabilities, .deck_snapshot = snapshot, .command_submit = submit, .command_poll = poll, .command_cancel = cancel, .command_release = release_result};
    core = (az_core_v1) {.abi = AZ_MOD_ABI, .size = sizeof core, .log = log_line, .data_dir = data_directory, .library_service = service, .thread_start = start};
    assert(az_mod_v1_entry()->init(&core) == 0);
    assert(strstr(last_log, "SDK_LOOP_READY v=1"));
}
static void drain(void) {
    probe_poll();
    probe_poll();
    assert(!probe.pending);
}
static void write_request(const char *text) {
    char path[1100];
    snprintf(path, sizeof path, "%s/request", directory);
    FILE *f = fopen(path, "w");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(!fclose(f));
}
int main(void) {
    strcpy(directory, "/tmp/sdk-loop-probe.XXXXXX");
    assert(mkdtemp(directory));
    reset();
    unsigned before = submits;
    for (unsigned i = 0; i < 3; i++) probe_step();
    assert(submits == before && !probe.pending);
    struct probe_input v = input(1);
    assert(probe_handle(&v) == AZ_OK && probe.pending && submits == before + 1);
    assert(strstr(last_log, "event=submit") && strstr(last_log, "outcome=0"));
    assert(probe_handle(&v) == AZ_STALE && submits == before + 1); /* no replay even while pending */
    terminal_outcome = AZ_COMMAND_ACCEPTED;
    probe_poll();
    assert(probe.pending && !probe.terminal);
    terminal_outcome = AZ_COMMAND_APPLIED;
    probe_poll();
    assert(probe.terminal && strstr(last_log, "revision=7"));
    release_busy = 1;
    probe_poll();
    assert(probe.pending);
    probe_poll();
    assert(!probe.pending && strstr(last_log, "event=release"));
    v = input(2);
    assert(probe_handle(&v) == AZ_OK);
    terminal_outcome = AZ_COMMAND_UNCERTAIN;
    drain();
    assert(strstr(last_log, "outcome=4") && strstr(last_log, "revision=0"));
    v = input(3);
    assert(probe_handle(&v) == AZ_OK);
    struct probe_input c = {.pid = (uint64_t) getpid(), .id = 3, .deck = 0};
    strcpy(c.action, "cancel");
    assert(probe_handle(&c) == AZ_OK);
    drain();
    assert(cancels == 1);
    before = submits;
    v = input(4);
    v.pid++;
    assert(probe_handle(&v) == AZ_STALE && submits == before);
    v = input(4);
    v.deadline = probe_now() - 1;
    assert(probe_handle(&v) == AZ_INVALID && submits == before);
    v = input(4);
    mask = 0;
    assert(probe_handle(&v) == AZ_UNSUPPORTED && submits == before);
    mask = 8190;
    v = input(5);
    v.load = 41;
    assert(probe_handle(&v) == AZ_STALE && !probe.pending && submits == before + 1);
    v = input(6);
    v.position = .25;
    v.op = AZ_CMD_LOOP_MOVE;
    assert(probe_handle(&v) == AZ_OK && submitted.position_s == .25 && submitted.operation == AZ_CMD_LOOP_MOVE);
    drain();
    struct probe_input snap = {.pid = (uint64_t) getpid(), .deck = 3};
    strcpy(snap.action, "snapshot");
    assert(probe_handle(&snap) == AZ_OK);
    assert(strstr(last_log, "event=snapshot") && strstr(last_log, "outcome=-1") && strstr(last_log, "load_valid=1") && strstr(last_log, "bounds_valid=1"));
    /* Exact transport parser rejects partial lines, extra keys, overflow and nonfinite values. */
    char line[512];
    snprintf(line, sizeof line, "v=1 pid=%ld action=submit id=7 deck=0 op=7 load=42 deadline_ns=%llu position=0 end=0 beats=0\n", (long) getpid(), (unsigned long long) (probe_now() + 10000000000ULL));
    struct probe_input parsed;
    assert(probe_parse(line, strlen(line), &parsed));
    assert(parsed.id == 7);
    assert(!probe_parse(line, strlen(line) - 1, &parsed));
    for (const char **bad = (const char *[]) {"v=1 pid=1 action=snapshot id=0 deck=4 op=0 load=0 deadline_ns=0 position=0 end=0 beats=0\n", "v=1 pid=1 action=snapshot id=0 deck=0 op=0 load=0 deadline_ns=0 position=nan end=0 beats=0\n", "v=1 pid=1 action=snapshot id=0 deck=0 op=0 load=0 deadline_ns=0 position=0 end=0 beats=0 extra=1\n", NULL}; *bad; bad++) assert(!probe_parse(*bad, strlen(*bad), &parsed));
    uint64_t integer;
    assert(!probe_integer("18446744073709551616", &integer));
    assert(!probe_integer("-1", &integer));
    assert(!probe_integer("01", &integer));
    write_request(line);
    probe_step();
    assert(probe.pending && submitted.request_id == 7);
    drain();
    char path[1100];
    snprintf(path, sizeof path, "%s/request", directory);
    assert(access(path, F_OK));
    assert(!symlink("/etc/passwd", path));
    probe_step();
    assert(strstr(last_log, "event=refused") && !probe.pending);
    assert(access(path, F_OK));
    assert(!mkfifo(path, 0600));
    probe_step();
    assert(strstr(last_log, "event=refused"));
    /* An immediately terminal successful submission still retains a record. */
    submit_outcome = AZ_COMMAND_APPLIED;
    v = input(8);
    unsigned released = releases;
    assert(probe_handle(&v) == AZ_OK && probe.pending && probe.terminal);
    probe_poll();
    assert(!probe.pending && releases == released + 1);
    /* A malformed ABI result is never promoted to a tracked SDK receipt. */
    for (int invalid = 1; invalid <= 3; invalid++) {
        bad_result = invalid;
        v = input((uint64_t) (8 + invalid));
        assert(probe_handle(&v) == AZ_OK && !probe.pending && !probe.terminal);
        assert(strstr(last_log, "outcome=-1"));
    }
    bad_result = 0;
    /* Results really reside in the owned regular file, beyond core.log's400B cap. */
    v = input(UINT64_MAX);
    v.load = UINT64_MAX;
    az_command_result wide = {.size = sizeof wide, .version = AZ_DATA_VERSION, .request_id = UINT64_MAX, .outcome = AZ_COMMAND_APPLIED, .reason = AZ_OK, .revision = UINT64_MAX, .observed_ns = probe_now()};
    long_bounds = 1;
    probe_emit("poll", &v, AZ_OK, &wide);
    assert(strlen(last_log) > 398 && strstr(last_log, "loop_ns="));
    char results_path[1100];
    snprintf(results_path, sizeof results_path, "%s/results.%ld.log", directory, (long) getpid());
    struct stat results_stat;
    assert(!stat(results_path, &results_stat) && S_ISREG(results_stat.st_mode));
    assert(init(&core) == -1); /* O_EXCL refuses a stale/current PID result file. */
    assert(!unlink(results_path));
    assert(!symlink("/etc/passwd", results_path));
    probe_emit("snapshot", &snap, AZ_OK, NULL);
    assert(probe.io_failed);
    assert(!unlink(results_path));
    assert(!rmdir(directory));
    core.size = offsetof(az_core_v1, library_service);
    assert(init(&core) == -1);
    assert(threads == 1 && snapshots > 10 && releases >= 4 && polls >= 4);
    puts("PASS public loop command probe: exact input, process binding, no replay, pending/terminal/cancel/release, stale loads and file guards");
    return 0;
}
