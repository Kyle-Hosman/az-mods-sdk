# Loop command probe

A normal-loader development client for exercising the public asynchronous loop
command API in a disposable firmware emulator. It installs no native hooks and
performs no operation until an explicit request file arrives. It uses only
`core->library_service(1)` and its own `core->data_dir`.

Build with `tools/azmod-pack build examples/loop-command-probe`, then package with
`tools/azmod-pack package examples/loop-command-probe`. Stage through the normal
loader and Allow UI. The package must be installed on the owned emulator USB so
the core can supply its genuine data directory before initialization. A missing
USB/data directory refuses initialization; there is no internal-storage fallback.

The ready record is:

```
SDK_LOOP_READY v=1 pid=<native PID> commands=<capability mask> data=<absolute data directory>
```

It repeats only if the command capabilities change. All records must retain the
same PID and data directory. Capability bits are operation IDs, not a count.

## Requests

Write a complete UTF-8 ASCII line to a temporary file in that data directory and
atomically rename it to `request`. The worker claims it as `request.processing`,
reads and removes that file. The maximum input is 511 bytes. Symlinks, FIFOs,
partial lines, extra fields and nonfinite numbers are refused. The exact field
order is:

```
v=1 pid=123 action=submit id=1 deck=0 op=7 load=42 deadline_ns=123456789000 position=0 end=0 beats=0
```

A trailing newline is required. PID must equal the ready record's native PID.
`deadline_ns` uses the same process's monotonic clock, must be in the future and
at most 30 seconds away at consumption. IDs are positive and strictly increase
for new submissions. Repeated/older submissions never execute again. Only one
SDK request is active at a time. This serial client can exercise each deck but
does not itself establish concurrent cross-deck submission acceptance.

Decks are zero-based. Operations are the public IDs 1–12. IN/OUT/ENTER/EXIT,
RELOOP/CLEAR/DOUBLE/HALVE use zero numeric arguments. LENGTH uses `beats`;
MOVE/ADJUST_IN use signed delta seconds in `position`; ADJUST_OUT uses signed
delta seconds in `end`. Quantization is always native and the request carries
`AZ_COMMAND_NATIVE_LOAD`; saved persistence is never requested.

Cancellation references the active command ID and deck; unused fields are zero:

```
v=1 pid=123 action=cancel id=1 deck=0 op=0 load=0 deadline_ns=0 position=0 end=0 beats=0
```

Read a public snapshot without submitting a command:

```
v=1 pid=123 action=snapshot id=0 deck=0 op=0 load=0 deadline_ns=0 position=0 end=0 beats=0
```

The client polls accepted requests until terminal and then attempts release.
Busy release is retried. Cancellation after dispatch does not promise rollback.
It stops after 1800 seconds or 4096 output records; `SDK_LOOP_DONE` states whether
a request remains pending. A test with a pending request is incomplete.

## Receipts

Full `SDK_LOOP` records are appended with one bounded write to
`data/results.<native PID>.log`. The core log contains only short READY, DONE
and I/O error messages: its line cap cannot carry the complete receipts. The
result file is created exclusively with mode0600 at initialization, reopened
with `O_NOFOLLOW`, checked as a regular file, and capped at2MiB. Existing PID
files, symlinks and nonregular paths are refused. I/O failure prevents further
input execution; already accepted work is polled/released where possible.
Read the core log and this result file together for the complete transcript.

Every `SDK_LOOP` record has exactly these fields:

```
v seq event id deck op target_load rc outcome reason revision receipt_ns query_ns load_valid native_load load_unit load_ns engaged_valid engaged bounds_valid in out loop_unit loop_rev loop_ns
```

Events are `submit`, `poll`, `cancel`, `release`, `snapshot` and `refused`.
`outcome=-1` means no command result is being reported (for example a snapshot
or local refusal); it never means accepted. Result receipt clocks come from the
SDK result, not the observer's query clock. `target_load` remains the submitted
load generation while `native_load` is the separately read current generation.
Unknown load/loop fields are zero or `engaged=-1`, qualified by explicit validity
flags. Bounds are source seconds. The independent loop unit/revision/receipt timestamp
qualify observation freshness; a terminal command receipt need not immediately
coincide with the GUI-collected loop publication. These public observations do not establish
saved-cue persistence, physical button ordering or audible timing.

Run `python3 -m unittest discover -s tests -p test_loop_command_probe.py` for the
actual client lifecycle/parser/file-guard fixture, plain and ASan/UBSan. These
synthetic tests do not execute EP147.

## Explicit worker batches (0.1.1)

The `refusals` and `matrix` actions use the same request fields. Set `op=0` and
all numeric arguments to zero. The starting ID must exceed every previously
used/reserved ID and be at most UINT64_MAX-15. PID and the future eligibility
deadline (at most30seconds) remain mandatory. For `refusals`, `load` is exactly
UINT64_MAX; for `matrix`, it is the explicitly observed current native load.
No plan starts without this explicit request.

A refusal batch submits all twelve valid loop operations with that stale load.
Every operation must return an actual SDK stale rejection. Unexpected acceptance
stops later submissions and is polled/released through the ordinary protocol.

A matrix batch performs EXIT, IN, OUT, DOUBLE, HALVE, LENGTH8, ADJUST_IN+0.05s,
ADJUST_OUT+0.05s, MOVE+0.05s, EXIT, RELOOP, EXIT, ENTER, EXIT and CLEAR. Each
request gets a fresh10second SDK deadline. IN application precedes OUT dispatch
by at least one second. Public loop-changing steps require a newer observation
on the same loop unit and native load before the next step; an observer wait is
bounded to five seconds. The first EXIT may have no loop observation, while IN
and CLEAR have no public retained-point reader and are receipt-only checks.

Every batch has a fixed180second overall deadline. Its whole ID range is
reserved immediately (12 or15 IDs), including unissued IDs if it stops early.
Ordinary submissions are busy while a batch runs; snapshots remain available.
Cancelling its pending ID prevents subsequent steps. Any stale load, non-applied
terminal result, timeout or I/O failure stops the plan; no rollback is promised.

Short `SDK_LOOP_PLAN` start/done records accompany the actual SDK_LOOP receipts
in the same results file. Their exact fields are:

```
v event kind id deck load status completed query_ns deadline_ns
```

`kind` is `refusals` or `matrix`; start status is INCOMPLETE. Done is PASS only
when all12/15 steps finish, FAIL for unexpected SDK results, or INCOMPLETE for
missing observations, cancellation or deadline/load loss. `deadline_ns` is the
fixed overall deadline. The separate host evaluator still checks the actual API
receipts and public postconditions; plan status alone is not firmware acceptance.
