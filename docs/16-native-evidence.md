# Native cue and loop research

This is an investigation ledger. Native commands remain disabled. An experimental working-loop
listener uses owned virtual interfaces and existing constructor hooks. No new absolute-address
mapping from this document has been added to generated tables or installed as a hook.

## Corrected working-loop observation

Physical 2.00 testing while playing exposed an incorrect interpretation: notification byte0
remained false through manual loop, EXIT and RELOOP. It describes a native designated mode,
not generic engagement. Earlier byte0-based claims are superseded by this section.

The named native `InstantDoublesTargetOnLocalPlayer::isActiveLoopPlay` methods
(1.30 `0xa609f0`, 2.00 `0xaa69c8`) use validity of the current working CueProperty.
Their PlayerState helpers (`0x1492ae8` / `0x154e058`) add `0xb18`, then invoke the
same five-field predicate (`0x13ec228` / `0x14a7668`). At borrowed notification offsets,
it tests byte `0x38 != 0`, low five bits of byte `0x3c != 0`, byte `0x3e != 0`,
little-endian u32 `0x40 != 0` and s64 `0x48 != INT64_MAX`. Position zero stays valid.

Both publishers copy working context `+0x160` to PublicStateSet `+0xb00`, which is
PlayerState `+0xb18`; checkLoopPlay copies that property to notification `+8`. EXIT
clears this working property (`0x13e3d60` / `0x149f058`) and increments its revision,
while retained slot pointers remain separate. The corrected engagement predicate applies those
bounded scalar reads during the callback. It retains no native pointer, and the unit
generation and receipt timestamp do not establish track-load identity or source capture time.

GNU7 fixtures now distinguish the mode byte from engagement and cover all five invalidity
branches, zero position, EXIT with separate retained points, RELOOP, wrong player and retired
generations. The corrected reader subsequently passed the physical playing sequence on
logical deck 1 of the 2.00 deck: off -> engaged -> off on EXIT -> engaged on RELOOP.
The same PID remained active with no automatic restarts. Other decks, paused behavior and
1.30 functional acceptance remain open. The prior byte0 build failed the functional test; registration success alone
is not a pass.

## Current working loop coordinates

Both builds embed `dj_player::PointProperty` at working CueProperty offsets `0x18` and
`0x70`, with `player_basis::PositionWithSourceInfo` at point offset `0x10`. Exact RTTI
names and main-ELF bounds guard the borrowed record; no new address lookup is used.
The sample positions are property offsets `0x40`/`0x98`, signed Hertz `0x60`/`0xb8`,
and cached microseconds `0x68`/`0xc0`. The inspected sample-first setters compute `(1e6 / Hertz) * samples`
then use FCVTAS (nearest, ties away from zero): 1.30 `0x13ef570`, 2.00 `0x14aa9b0`.

The decoder separately validates each source-position predicate, equal 12-byte source IDs,
equal positive Hertz, nonnegative forward positions, finite conversion and consistent cached
microseconds. It compares a nonzero borrowed source association without dereferencing or
retaining it. Integer values above `2^53` are conservatively unavailable to preserve exact
integer-to-double conversion. No application-specific duration or audio-rate policy is added.
Engagement remains independent of range validity; EXIT invalidates current bounds without
inventing retained state. Private observation v2 preserves v1's original 40-byte ABI.

The later Deck 2 capture disproves the SDK's former exact-cache assumption as an availability
policy: IN81673 at44100Hz has cached1852000us, a difference of0.2 sample. The SDK now permits
one source sample plus half an integer microsecond of cache rounding, using exact wide integer
comparison. Source seconds remain samples/Hertz. The cache writer producing that particular
record has not been identified; the policy does not assert an unproved native setter route.
The captured values pass the corrected synthetic observer; the new build's hardware reload
and functional acceptance remain pending.

GNU7 fixtures cover zero IN, captured 44100 Hz coordinates, 48/96 kHz, rates and durations
outside Anchor's application policies, rounding ties, invalid identities/rates/RTTI/sentinels,
reversed points, EXIT/RELOOP and concurrent publication/reconfiguration. Plain and ASan/UBSan
pass; LeakSanitizer under Docker emulation remains unavailable. Both-build static layout
agreement and synthetic tests do not replace native functional acceptance. In the repeated
physical 2.00 sequence, engagement is correct but every working-boundary observation remains
unavailable. Exact final queued construction preserves the five expected native types and
rate/cache offsets on both builds. A first-failure scalar diagnostic is being prepared to
identify the failed check; the additional source-owner guard is not a native invariant and
must not be assumed to explain this observation without runtime evidence.

## Target identity and coverage

The supplied EP147 2.00, 2.01 and unsuffixed EP147 copies are identical: 62461736 bytes,
MD5 `8daa1de55d7328f458f6d855cdc3cdde`, SHA-256
`6577f42532375936c7e3fa830efa35b7daa9cde8e0a265c1fc7143f824321925`.
A recursive ordinary-file scan of the supplied firmware folder found no standalone 1.30
EP147 executable. Archives and filesystem images were not exhaustively decoded; this is
not a claim that no older executable could exist inside every container.

The subsequent official
[1.30 update package](https://downloads.support.alphatheta.com/firmwares/all-in-one-dj-systems/XDJ-AZ/XDJAZv130.zip)
was downloaded for offline comparison. ZIP SHA-256 is
`fed8d12d1168ac79a4d6e4d8c58531c32eb108f3f56a899f1b7edcc30df1e0c7`.
Using the operator-supplied updater key locally, its LUKS payload was decrypted without
mounting or executing it. The kernel's gzip member at file offset `0xba3808` validates and
expands to a 258588160-byte newc archive; parsing reaches `TRAILER!!!` after 9377 entries.
The extracted `home/root/pdj/EP147` is 57874328 bytes, MD5
`3518275e8b4d5d63636d8d7b4468d7ba`, SHA-256
`736bdc9322c00e5770af459c92cace33d8680825c07f00f909f74dfc473a77a6`.
This matches the checked-out core's known 1.30 build. Firmware and keys stay outside the
contribution. No deck firmware was installed or changed.

REA's GNU objdump inspection establishes the ELF's AArch64 little-endian executable format
and entry `0x6addc4`. The complete records are retained outside the public source tree:

- File-header Evidence: `ev_02113bd062262430facbc63712cab8622b6154f2631e3e91ccc4f4cb32d73edb`.
- Section-header Evidence: `ev_fc2831a535327a6e3fe650c3bf7d7eb5ea050cc31628dc51ad81e74ccaa77735`.

Both operations report unchanged source bytes and complete command output. They establish
layout, not runtime behavior or native signatures. REA's initial Ghidra import exceeded its
330000 ms startup deadline. The focused retry completed with these analyzers disabled:
Stack, Basic Constant Reference Analyzer, GCC Exception Handlers, Demangler GNU and
Decompiler Switch Analysis. Its analysis profile digest is
`eb44c8715b6287abf53e7c64bb53b403af57eacc05f0948a11259f7942976cfa`.

The RELOOP dossier is Evidence
`ev_fccf6e1351b7569200cd8fc933ba3a7f15381c2b0a50e7d70fae38085d850368`.
It covers the contiguous function body `0x1422eb8` through `0x14233f7` (1344 bytes),
15 resolved callees and nine unresolved calls. Relationship coverage is explicitly partial.
The empty returned caller list does not prove there are no callers. Its API view is Evidence
`ev_e02672b24147b87b0eecadc5919e07fe63bcb1e31f8c579e308c7b77d89a0f02`.
Ghidra recovers an eight-byte receiver in x0 and a four-byte argument in w1, both at low
confidence. This is decompiler evidence, not an approved C function declaration or verified
enum contract. The focused result corroborates the asynchronous-task branch observed in the
saved database, while omitted analyzers and unresolved indirect calls remain limitations.

The observations below also use the existing saved Ghidra analysis database, opened read-only
with automatic analysis disabled, and targeted xrefs/decompilation. Those observations are
ordinary Ghidra evidence, not fabricated REA Evidence records. Raw pseudocode, instructions
and track/firmware bytes remain outside the contribution. Addresses and behavioral descriptions
are recorded here so the table maintainer can reproduce the investigation.

## Observed static paths

| Candidate | 2.00 entry | Static observation | Still unknown |
|---|---:|---|---|
| CueLoopControlFacade reloop | `0x1422eb8` | Xref at `0x1423250` reaches the native reloop/SlipBehavior signature string. The body checks control availability, has a direct fallback, allocates an asynchronous task carrying the facade pointer and a 32-bit argument, then passes it through the native task box. | SlipBehavior values, safe caller context, facade acquisition/pinning, owning thread, completion and actual RELOOP behavior |
| CueLoopControlFacade jump | `0x14261d0` | Xref at `0x142655c` reaches the CueKind/HotCueBehavior/SlipBehavior signature string. | Enum values, seek/start semantics, thread ownership and quantization |
| CueLoopSetFacade setHotCue | `0x1434a40` | Xref at `0x1434e40` reaches the CueKind/RgbColor/QuantizeSetUnit signature string. | Argument ABI, persistence, native object lifetime and result observation |
| CueLoopSetFacade setActiveLoop | `0x14361e8` | Xref at `0x1436494` reaches the native setActiveLoop/CueProperty signature string. | Saved automatic activation versus current engagement semantics; no conflation is permitted |
| CueProperty setOutPoint | `0x14a17e8` | The body compares supplied Hertz against the cue's stored sampling rate before delegating the position update. The signature string is referenced at `0x14a1834`. | Full Position/Hertz layouts, source-rate conversion and mutation lifetime |
| Command-chain request wrapper | `0x14197f0` | Xrefs at `0x1419808`, `0x1419810` and `0x14198ac` identify the command-chain request diagnostic. The body delegates to `0x140c570` using its manager member. | Command-chain argument/ownership rules, dispatch order and cancellation |
| StateNotificationServer beat-sync observer | `0x153bde0` | The body copies a weak observer control block into an asynchronous addObserver task. | Safe observer creation, listener interface ABI, shutdown and callback thread |
| StateNotificationServer master-request observer | `0x153ca80` | The body uses the same weak-observer/asynchronous registration pattern for a different listener. | The same ownership/thread questions; this is not a loop-state observer yet |
| Instant-doubles active-loop query | `0xaa69c8` | Requires a player-state pointer and calls `0x154e058`; the latter adjusts its receiver by `0xb18` and tail-calls `0x14a7668`. | Whether this represents current loop engagement, saved auto-activation, or a narrower instant-doubles state; native tests are required |

The native queue is substantive evidence against invoking command implementations from an
arbitrary SDK worker. It does not establish that facade entry methods themselves are safe on
any arbitrary thread, or that retaining a facade pointer is safe after a track/deck change.

## Ownership leads

Read-only xrefs to the CueLoopControlFacade address point `0x2bc8128` and CueLoopSetFacade
address point `0x2bc9bd0` both lead to `0x1565b88`, a construction/assembly candidate.
StateNotificationServer's address point `0x2be9d10` is referenced by `0x1537570` and
`0x1545298`. CueLoopFunctionHandler's address point `0x2bd2b28` is referenced by `0x1460168`
and `0x1460480`. These are leads for constructor/destructor and dependency inspection,
not proof of object ownership or a deck identity. Further decompilation identifies
`0x155d9f0` as the existing 2.00 DjPlayerUnit constructor (the address already selected by
`EP_14a2560`). It allocates a 0x78-byte bundle, calls `0x1565b88` at `0x155df00` and stores
that bundle at unit+0x190. The bundle stores the 0x250-byte composite cue facade allocation
at +0x40; its set/load subobjects start at composite+0xc8/+0x190. The existing 2.00
DjPlayerUnit destructor `0x155d700` clears unit+0x190, invokes bundle cleanup
`0x155fa08`, then releases the allocation. This corroborates unit ownership, not arbitrary
thread access or permission to retain pointers. The current unit registry does not itself
pin these objects for asynchronous SDK use. Queue draining and in-flight task lifetime still
need verification before dispatch can be enabled.

Additional raw RTTI/vtable candidates include LoopCommand (`0x2bce328`), BackCueCommand
(`0x2be1700`), CueProperty (`0x2be12b0`) and CuePropertyWithBufferLock (`0x2be14d0`). A vtable
match or an accessible pointer does not establish a valid live object. Read/write offsets
must not be promoted into SDK behavior without owning-thread capture and lifetime evidence.

## Standalone tests on the authorized 2.00 deck

The expanded current run passes 27 checks: cache/parser, disk provider, pure waveform
formats, real ANLZ, real streamed PWV3/PWV5/PWV7 windows, public deck projection, loaded
observations, v1/v2 loop exports and the GNU listener, each in plain, ASan/UBSan with leak
detection enabled, and TSan. Fourteen planted faults are detected, covering source identity,
old output prefixes, wrong players, retired generations, mode-byte confusion, rejecting time
zero, window key collisions, ID padding, both export sizes and sanitizer instrumentation.
EP147 retains its PID and loaded-deck state across the standalone run. This is fixture
validation on a native kernel, not in-process firmware behavior. The earlier nine-check run
below records the initial narrower validation.

The operator-authorized deck identifies as AArch64 and its EP147 hash matches the 2.00 artifact
above. Separate test processes passed parser/cache, the actual disk adapter's fake-source
SQLite fixtures and the supplied real ANLZ pair, each in plain, ASan/UBSan with leak detection,
and TSan runs (nine successful checks). A broken source-identity implementation was rejected;
an intentional out-of-bounds store and an intentional data race produced their required
sanitizer diagnostics. EP147 PID 8072 and deck state `LLEE`
were unchanged across that run. Temporary binaries were placed under a unique `/tmp`
directory; no core library, setting, approval record or firmware was changed.

These tests validate standalone production parser/cache/provider code on the deck's kernel.
They do not validate new services inside EP147, live native snapshots, audible operations,
1.30 runtime behavior, latency or freshness. `tests/sdk-data-deck.sh` reproduces the standalone
checks with strict SSH host verification and diagnostic-checked negative controls.

## Requirements before native implementation can be enabled

1. Verify object construction/destruction, deck assignment and reference/pinning rules.
2. Recover the actual queue and enum/argument contracts, including exception boundaries.
3. Establish authoritative readers and true load/cue/loop revisions before accepting commands.
4. Resolve corresponding 1.30 entries and layouts, or use the existing optional-feature
   pattern with explicitly limited support and maintainer-regenerated mappings.
5. Test native behavior, observed completion, physical-control ordering, quantization,
   persistence, deadlines and cancellation on an identified emulator/test deck.

Static decompilation has not observed execution. Neither paused/playing command effects nor
hardware latency/freshness is established. All public command capability bits remain zero.

## Load-generation and dispatch boundary research

Both builds have an accepted `onAboutToLoadTrack(LoadedOneTrack const&, LoadType const&)`
callback at LoadState secondary slot4. It fires after the native load-lock check, before
prior unload, and the examined branch does not suppress identical TrackIDs. This establishes
an accepted-attempt event, not a committed load generation.

Completion ABI differs: 1.30 secondary slot10 forwards Wherefrom, LoadType, Result and
DownloadResult; 2.00 slot13 adds a final u64. Native LoadRequestID is a polymorphic prefix
with a process counter at+8, and async handlers compare the current ID during cancellation.
The final controller wrapper discards that ID; later completion gates inspected so far use
stage plus12-byte TrackID. No cross-queue acknowledgement has been proved to exclude stale
same-track completion. An accepted ordinal, busy=false or pointer identity must therefore
not be published as committed generation. Failure and eject paths remain under investigation.

Native task allocation and diagnostic ABI also need care. The task base token is a lazy
process value, not a firmware enum. Verified token helpers are1.30 `0x6eec80` /2.00
`0x7305f8`; default-empty native String constructors are1.30 `0x230e3c0` /2.00 `0x247b860`.
A private task would need those provider-backed helpers, native allocation/ownership,
nontrivial eight-byte String return through AArch64 X8, generation checks at execution and
pending-task teardown. These addresses are static evidence only; they are not new runtime
lookups or generated-table additions. Required command-state, queue, lifetime and applied
completion tests remain open. All command bits stay zero.

## Physical service and loop observations

The single inspector is confirmed loaded through the shared ABI-1 service on the physical
2.00 deck. It displays real USB saved cues/hot loops,1025 beats,25 phrases and a bounded
4096/74406-column mono window. Its chart is visible in the combined page. Native commands
remain unavailable. The loaded-state observation shows the actual loaded/empty transition.

Playing EXIT/RELOOP follows engaged -> off -> engaged. One loop has source positions
143746/309121 at44100Hz, with cached3259546/7009546us; this gives IN3.259546485s,
OUT7.009546485s and duration3.75s. A later held length edit advances the publication revision
and changes OUT to185046samples/4.196054422s (duration0.936507937s), matching the native
shortened-loop display. Both cached times match the strict source-sample conversion.
Some earlier engaged payloads were withheld by the former exact-cache comparison;
those inputs were not retained in that earlier build. The later retained Deck 2 case above
shows why a sub-sample difference alone must not be treated as invalid native loop timing.

The later record was read from the core's own static atomic publication, with a matching
module SHA, compiler-derived layout/symbol bounds, matching sequence/active generation
before and after, and stable process identity. No EP147 objects or entry points were used,
and no thread was stopped. The short-lived log had ended before later edits; absence from
that log was not evidence that the native channel ignores boundary changes.

Static timer/trigger research corroborates revision-driven updates: working boundary and
length helpers increment property+8; publication and refresh copy that revision into
PlayerState+0xb20; checkLoopPlay compares it with the last notified revision. The timer
requests a10ms interval, but complete write-side epoch cadence, delivery latency, other
decks, paused cases and1.30 runtime behavior remain unverified. This is partial acceptance,
not a completed live-state release.
