# Public SDK wave layer probe

This development mod checks the stored-analysis encoding8 feature through
`library_service(AZ_LIBRARY_ABI)`, public library enumeration/resolution and public
waveform tickets. It uses the normal community-mod loader. Its only exported
entry is `az_mod_v1_entry`; it installs no hooks and sends no controls.

`PROBE_SOURCE_PORT = 0` explicitly selects USB1. The managed worker takes the first
callback from `lib_tracks(id, 0, 0, ...)`, copies only its public ID, and resolves
that ID on the same port. It does not search for another track when PWV7 is
missing. It neither reads a deck nor matches a title, native ID or source to a
loaded track. There is no library/file work in init or a render callback.

The worker fetches one raw encoding3 column to discover the stored extent. It
then requests raw3 and derived8 for the same resolved public reference and middle
window of up to64 columns, followed by both encodings at the last column with a
4096-column request to verify clipping. An independent inverse decoder checks
the full returned window: legal symbolic subset IDs, strict magnitude/membership
progression, zero unused fields, reconstructed raw bytes and SDK-reserved byte5
zero. It does not sort the source bytes or invoke the core encoder.

Both windows must have matching public reference fields, exact requested/clipped
geometry, strides3/6, FILE/DERIVED provenance, ready immutable data and the fixed
150-column/s analysis axis. Each encoding is also tested with count0 and first=total,
which must produce `AZ_INVALID`, and count4097, which must produce `AZ_LIMIT`. Range rejection can be immediate
or a terminal acquire result. Unknown encoding99 must produce `AZ_UNSUPPORTED`.
Every accepted ticket is released, including terminal errors and timeout paths.

The worker has at most two tickets at once,24 request/resolve retry attempts,
50ms retry delays,256 ordinary service calls and a30-second monotonic deadline.
Pending acquisitions use those existing global call/deadline bounds rather than
assuming the asynchronous worker finishes within24 polls. Cleanup has
an independent bounded reserve of24 release attempts per cleanup invocation.
Service I/O latency itself belongs to the public core call; the worker checks its
deadline between calls and cannot interrupt a blocking service implementation.
It emits at most48 log records, each shorter than768 bytes. Hashes are FNV-1a64
over every byte of the bounded window, not file identities or cryptographic
digests. Hex samples contain at most18 bytes from each payload.

All lines use the whitespace-separated grammar:

```
SDK_PUBLIC_WAVE v=1 seq=N port=0 event=EVENT key=value ...
```

`resolve` records the selected opaque public source/generation/library/track
reference. `query` includes that reference, ticket, query time, encoding, first,
requested count and API status. `view` records returned geometry, provenance,
revision/time and the reference. `payload` records full-window hashes and small
raw/layered hex prefixes. `release` records the ticket and release status. The
last `final` line has `status=PASS|INCOMPLETE|FAIL`, a single-token `reason`, call
and accepted/released/outstanding ticket counts. PASS requires all payload,
geometry and refusal checks. An absent service/track/format, old core rejecting8,
pending/busy timeout, clock/deadline or cleanup failure produces INCOMPLETE.
Contract or payload violations produce FAIL. No titles, paths, addresses or key
material are logged.

Host checks, from the SDK root:

```
python3 -m unittest discover -s tests -p test_public_wave_layer_probe.py -v
AZ_CORE_TEST_ROOT=/path/to/core-checkout \
  python3 -m unittest discover -s tests -p test_public_wave_layer_engine.py -v
```

These use a fake public service and fixed independent payload cases. They check
the public descriptor, managed-worker boundary, selected source/ref, ticket
ownership, terminal/immediate refusals, malformed data, timeout cleanup and
independent mutation controls in plain C and ASan/UBSan. They do not establish
firmware, all-deck or emulator acceptance. The engine integration additionally
drives the actual bounded queue and stored-file provider with a fixture-owned
canonical PWV7 file, including delayed worker pumping and real limit statuses.
Enumeration, source references and scheduling remain explicit test fixtures.

The future emulator procedure is prepared, not executed by these host tests:

1. Build/package this directory with the coordinated pinned SDK toolchain and
   core/header packet: `tools/azmod-pack build examples/wave-layer-probe`, then
   `tools/azmod-pack package examples/wave-layer-probe`.
2. Install through normal Community Mods handling, select Allow for the exact new
   package and apply/reload through that supported flow. Retain the package/core
   hashes and show that the restarted EP147 PID differs from the pre-apply PID.
3. With the intended local library available on explicitly selected public port0,
   collect the bounded `SDK_PUBLIC_WAVE v=1` sequence. Require final PASS with
   outstanding0, inspect all queried/returned refs,3/8 geometry and FILE/DERIVED
   provenance, both clipped tails, payload hashes/samples and all refusal statuses.
4. Repeat under each exact firmware packet required by the acceptance plan.
   Keep INCOMPLETE/FAIL results and their reasons; do not promote a host fake pass
   or an unrelated state-probe run to this encoding8 acceptance.

The feature concerns uncalibrated stored analysis and symbolic subsets. It claims
no native player pixels, gain, frequency calibration, source/load association or
command completion.
