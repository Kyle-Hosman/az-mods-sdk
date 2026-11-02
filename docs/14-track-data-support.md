# Core SDK support and evidence matrix

All 26 operation IDs have native development paths through the public library
service. Runtime support requires behavioral tests for each operation/profile;
source presence alone is not a functional pass. Broader SDK gaps below are
follow-up work unless a specific command needs that dependency.

The portable file provider is development-only on EP147 1.30 and 2.00. Version 2.01 shares
2.00's executable identity. This table describes implemented host logic, not hardware
certification. The command section records the separate public-SDK firmware-emulator
checks. Broader native-state acceptance remains open. Earlier physical 2.00
observations are recorded below and do not certify all commands on hardware.

Waveform processing and PRO DJ LINK support are out of scope and are not SDK completion
requirements. Existing raw waveform APIs are retained; further decoding, calibration,
rendering and format expansion are not required.

All file rows below support only physically resolved local USB exportLibrary.db libraries.
PRO DJ LINK is not required. Streaming, cloud sources and legacy PDB-only libraries remain unavailable.
Both build columns have the same portable implementation; native rows inherit existing
address tables and are not newly certified. No mappings were added to generated tables.

| Field | Meaning / units | Source and evidence | 1.30 and 2.00 development behavior |
|---|---|---|---|
| Track source port | Physical USB 0/1 | sdk_stick_mount / sysfs port rule | File-backed; unknown port fails |
| Source connection generation | Session monotonic token | mountinfo mount ID + exact database dev/inode/path | File-backed; nonwrapping connection token; exhaustion refuses |
| Library identity | Connection database identity | stat database dev/inode | Session-local fingerprint; compare the complete reference, permanent UUID unavailable |
| Track ID | rekordbox content_id | Exact selected exportLibrary.db row | File-backed; duplicate-source IDs stay separate |
| Title | UTF-8, bounded 191 bytes | content.title | File-backed |
| Artist | UTF-8, bounded 191 bytes | artist.name joined by artist_id_artist | File-backed |
| Album | UTF-8, bounded 191 bytes | album.name | File-backed |
| Duration | Original seconds, integer precision | content.length | File-backed; NULL/zero unavailable |
| Original tempo | BPM | content.bpmx100 / 100 | File-backed; positive only |
| Key | UTF-8, bounded 31 bytes | key.name | File-backed |
| File location | Bounded absolute path within source | content.path + selected mount | File-backed; escaping paths rejected |
| Audio format | Codec/sample format | No verified source | Unavailable |
| Deck loaded/empty | Boolean | Copied12-byte LTI value; trailing padding ignored | Native observation; inherited reader |
| Deck track reference | Source-qualified association | Not established from raw TrackID | Unavailable; no content/title matching |
| Deck load generation | Source-qualified load identity | No exact source/track join | Unavailable; native receipt is separate |
| Native load generation | Matched native completion identity | Independent native load observer; unit generation/invalidation/time | Native command target with AZ_COMMAND_NATIVE_LOAD; no USB/library/track association |
| Deck observation revision/time | Capture sequence / monotonic ns | Atomic LTI snapshot under its sequence lock | Native copy; single read attempt |
| Playback state | Paused/playing/cueing | No verified native reader | Unavailable; legacy movement heuristic unchanged |
| Position and direction | Source seconds / direction | Legacy playhead lacks new load association | Unavailable in new contract |
| Playback rate | Ratio | No load-coherent provider | Unavailable |
| Current tempo | BPM, conservative20..400 range | Owned native BpmState GUI sample; independent generation/revision/time | Host/ARM regressions and build pass; new runtime acceptance pending, no track-load association |
| Key shift | Semitones | No verified coherent provider | Unavailable |
| Master Tempo | Native setting boolean | Both-build registered MasterTempoSetter maps the strict KeyLock receipt to MasterTempo; shared key-lock revision/time | Development opt-in; public API changes pass all4 decks on both builds; independent unit receipt, no source/load association |
| Sync enabled setting | Native mode0/off or2/on | Owned slot2 weak notification, both-build registered SyncModeSetter mapping | Development opt-in; public API changes pass all4 decks on both builds; mode1/other values unavailable |
| Master role | Local deck boolean | Existing MasterPlayer confirmed post-exchange event; shared revision/time | Host public regression passes all4 decks; runtime acceptance pending, no track-load association |
| Quantize | Native boolean setting | Independent weak notification, native-live development flag | Experimental; valid only after its own receipt |
| Raw play flags | Three copied native bytes | Notification triggered by play_pause changes only | Experimental; mode/aux are context, normalized playback unavailable |
| Raw key lock | Native boolean setting | Independent weak notification, native-live development flag | Experimental; the same valid receipt also supplies Master Tempo |
| Slip and slip position | Boolean / source seconds | No verified provider | Unavailable |
| Main cue | Temporary cue point | No verified reader | Unavailable |
| Hot cue slots | Point or loop, slots 1..8 | PCO2/PCP2; fallback PCOB/PCPT | Saved file data |
| Memory cues / saved loops | Revision-scoped ordered entry | PCO2/PCP2; fallback PCOB/PCPT | Saved file data |
| Cue loop boundaries | Original source seconds; explicit validity | PCP2 +0x14/+0x18; PCPT +0x20/+0x24 | Saved file data; zero start valid |
| Cue comment | UTF-16BE decoded to bounded UTF-8 | PCP2 length +0x28, payload +0x2c | Saved file data |
| Hot cue color | 0xRRGGBB under AZ_CUE_COLOR | PCP2 RGB after comment | Saved file data when trailer exists |
| Memory cue color | Raw palette ID under AZ_CUE_COLOR_ID | PCP2 byte +0x1c, color_id/reserved union | Saved file data; zero valid, no RGB palette inferred |
| Native cue-set changes | GUI collection revision / monotonic ns | Owned Unit slot8 weak listener; copied opaque routing tag | Implemented under native-live opt-in; host/SAN/ARM all4-deck regressions pass, runtime acceptance pending. Includes load/reset; no cue-kind or save claim |
| Cue persistence / modification | Saved boolean / unknown modifications | File provider, never live edits | Saved true; modified unknown |
| Cue automatic activation | Saved Active Loop property under AZ_CUE_AUTO_ACTIVE | PCPT loop status 0/1/4; exact unique legacy counterpart for PCP2 | Saved file property; native command activation/persistence covered below |
| Current working IN/OUT | Source seconds, zero IN valid | Bounded copied native property, checked samples/Hertz | Experimental; separate per-observation validity, runtime acceptance pending |
| Retained points / defined | Independent native state after EXIT | No verified reader | Unavailable |
| Loop engaged | Current working-loop validity | Owned UnitStateServer / StateNotificationServer virtual listener | Experimental, separately gated; unknown until a valid callback |
| Reloop / loop mode / saved association | Independent native state | No verified reader | Unavailable |
| Complete beat grid | Original seconds, BPM, beat-in-bar | PQTZ header 24, entries 8 bytes | File-backed; strictly increasing time |
| Phrases | Grid boundary indexes, raw kind/mood | PSSI versions 0/1, header 32, u16 stride 24..64; XOR variant | File-backed; unknown versions unsupported; natural v1 export pending |
| Analysis availability/provenance/revision | Per-component status and observation | Immutable publication | File-backed; failures do not publish partial arrays |
| Time/beat conversion | Fractional zero-based beat index | Adjacent original grid times | Derived; no extrapolation |
| Loop/interval source length | Source seconds | End minus start | Derived; valid zero duration allowed |
| Loop/interval beat length | Fractional beats | Full grid conversions | Derived when both boundaries covered |
| Loop/interval bars | Bars | No reliable meter model | Unavailable; no 4/4 assumption |
| Playback duration estimate | Seconds at explicit ratio | Source duration / positive caller rate | Derived estimate; separate validity |
| Overview waveform | Raw PWAV bytes, at most 4096 | PWAV header >=20, count at +0x0c | File-backed; approximate mapping from DB duration |
| Mono detail waveform | PWV3 packed height/intensity, 150 columns/s | Bounded streaming range provider | Development local USB; max4096 columns/request |
| RGB / three-band detail | PWV5 BE16 / PWV7 low-mid-high,150 columns/s | Bounded lossless streaming windows | Development local USB, when canonical tag exists |
| Raw layered detail | PWV7-derived symbolic subset IDs and absolute magnitudes, six bytes/column, 150 columns/s | Bounded immutable conversion; SDK reserved byte zero | Development local USB; derived provenance, calibration unavailable |
| RGB / three-band preview | PWV4 raw six-byte / PWV6 raw three-byte columns | Canonical headers 24/20; bounded streaming windows | Development local USB; approximate duration/count axis, usable metadata required |
| Mono / tiny preview windows | PWAV / PWV2 raw byte columns | Canonical 20-byte count/header-word layout; DAT streaming windows | Development local USB; approximate duration/count axis, usable metadata required |
| Calibrated waveform rendering | Native gains/gamma/frequency cutoffs | Not verified | Out of scope; not required for SDK completion |
| Change events | Owner-scoped sequence, ticket, revision, time | 64-entry polling publication journal | Implemented for track data/invalidation; missed flag |

Native ownership evidence: `deck_track_map.c` documents LoadState's constructor signature
(eight arguments; deck in w4, checked against magic and deck field), destructor invalidation,
and LTI update ownership. Its update hook forwards all eight arguments and copies on the
native writer thread. LTI layout is TrackID +0x08 (16 bytes), title +0x38 and artist +0x40;
LoadState owns LTI at +0x150. The core never exports those pointers. The new observation
export reads only the atomic copy and validates its owner token and sequence. The existing
vtable/address mappings and constructor/destructor hooks are reused. An optional C++ listener
registers through the owned publisher virtual interface; it adds no absolute-address lookup. Its weak ownership, player argument and retired-generation protection have native GNU C++ ABI
fixtures and mutation controls. Submission succeeds for four units on both emulated builds
and physical 2.00. Actual notification behavior and teardown still require acceptance. The
listener publishes a separate receipt timestamp and unit generation, never a load generation.

File ownership evidence: `sdk_track.c` owns each read-only SQL connection until it has copied
the row; statements and connection close before publication. It resolves paths only within
that exact mount. `sdk_analysis.h` parses bounded in-memory input on the core worker.
Snapshots own all returned arrays until every ticket releases. The native writing thread
never performs file reads, allocation or parser work for these services.

## Commands

Transient loop operations 1–12 use the shared asynchronous native backend for
both firmware profiles. Enable `azmod.track-data.on` and `azmod.native-load.on`
before startup. `azmod.native-loop-commands.on` enables the ten control/adjustment
operations below; EXIT and RELOOP retain their separate `azmod.native-exit.on`
and `azmod.native-reloop.on` flags. Check each operation's capability bit.

Use `AZ_COMMAND_NATIVE_LOAD`, a valid snapshot `native_load_generation`, zero
`expected_revision`, and `AZ_QUANTIZE_NATIVE`. Unused numbers, cue selectors,
color and comment must be zero/empty. No public structure layout changes.

| Operation | Arguments and behavior |
| --- | --- |
| Loop IN | All numeric arguments zero. Capture current playhead into native default cue/loop IN; use device-configured quantization. Arbitrary absolute IN is unsupported. |
| Loop OUT | All numeric arguments zero. Capture current playhead and enter native loop; requires inactive working loop and valid retained IN. Arbitrary absolute OUT is unsupported. |
| Loop ENTER | All numeric arguments zero. Native RELOOP alias, including seek to retained IN; already-engaged requests finish uncertain. |
| Loop EXIT | All numeric arguments zero; native exit, with inactive postcondition. |
| RELOOP | All numeric arguments zero; native reloop, requiring inactive-to-engaged transition for confirmation. |
| Clear retained points | All numeric arguments zero. Clear native kind0 retained cue, including the temporary main cue; this does not directly clear the separate working loop. |
| Double / halve | All numeric arguments zero. Scale existing engaged working loop; confirmation checks unchanged IN and the scaled beat ratio (beat mode) or sample length (time mode). |
| Beat-based length | Positive `beats` exactly representable as a nonzero unsigned16 numerator/denominator rational; other numeric arguments zero. Change existing engaged loop, preserving IN. |
| Move loop | Signed nonzero `position_s` delta in source seconds; other numeric arguments zero. Two native boundary adjustments, outward boundary first. Non-atomic; partial application is uncertain, with no rollback. |
| Adjust IN | Signed nonzero `position_s` delta in source seconds; other numeric arguments zero. |
| Adjust OUT | Signed nonzero `end_s` delta in source seconds; other numeric arguments zero. |

Adjustments require an engaged loop with coherent source points. IN adjustment
and MOVE additionally require native default-cue selection (kind0) and mode380
equal to zero; other selections/modes are rejected before dispatch. Native minimum
policy0 enforces approximately13.33ms. OUT/length use native LoopType1 to skip
the selected-retained-cue copy branch. Beat-length application can still
synchronize the temporary default cue with the working loop; callers must not
assume that temporary cue stays unchanged. IN has no LoopType argument: its native
route can copy the working cue into the selected retained cue; MOVE inherits
this side effect. These are transient native edits, not verified saved-track
persistence. Native operations can reposition playback.

All operations share per-deck FIFO ordering, load/lifetime checks, deadlines,
cancellation and correlated Slip/BeatSync/Quantize descendant tracking. Poll
`command_poll` until terminal, then `command_release`. `ACCEPTED` can span
processing frames. `APPLIED` requires settled descendants and the operation's
checked postcondition on the same load; clamped/no-op or partial adjustments
cannot count as successful exact edits. Its revision is the shared transient
confirmation counter, independent of deck, loop and database revisions.
Cancellation after dispatch cannot undo an audible operation.

Saved cue writes require `AZ_COMMAND_PERSIST`. Revision-selected operations
also use `AZ_COMMAND_NATIVE_CUE_IDS` and the exact revision/entry ID returned by
`native_cues`; file-provider IDs are not interchangeable with native IDs. Saved
and Memory recall use native-load/native-cue-ID flags without the persistence
flag. Poll to a terminal result, release it, and refresh the cue snapshot to the
confirmed revision. Memory RGB accepts the native palette; Hot RGB supports the
requested 24-bit color. See the public header for exact operation arguments.

| Operation | Native confirmation |
|---|---|
| Loop IN | Captured source-bound default cue IN; independently observed playhead motion |
| Loop OUT | Captured OUT, preserved IN and engaged working bounds |
| Loop ENTER | Retained bounds restored, engagement transition and source seek; already-engaged requests remain uncertain |
| Loop EXIT | Disengaged working loop, retained bounds and unchanged transport |
| RELOOP | Retained bounds restored, engagement transition and source seek; already-engaged requests remain uncertain |
| Clear retained points | Default cue cleared while the independent working loop stays engaged |
| Double / halve | Unchanged IN and expected native sample length or beat ratio |
| Beat-based length | Native unsigned16 rational beat length and preserved IN |
| Move loop | Both source-second boundary deltas; partial application remains uncertain |
| Adjust IN / OUT | Requested source-second delta within one source sample; native clamping cannot confirm an exact edit |
| Recall saved loop | Selected source bounds, seek and loop engagement |
| Save loop | Correlated native reply and added saved-loop entry |
| Delete saved loop | Correlated native reply and selected entry absent |
| Set saved automatic activation | Previous loop disabled and selected loop enabled; partial writes cannot count as complete |
| Trigger Hot cue | Selected point/loop, source seek and actual playing transport |
| Create/replace Hot cue or loop | Stored point/loop bounds and color, with other cues preserved |
| Clear Hot cue | Selected slot absent, with other cues preserved |
| Recall Memory cue or loop | Selected source position; a point pauses and disengages the loop, while a loop restores its bounds |
| Create Memory cue or loop | Stored point/loop, correlated native reply and collection change |
| Delete Memory cue or loop | Selected entry absent, with other cues preserved |
| Set main cue | Native temporary point equals the captured source position |
| Recall main cue | Source seek to the temporary point and paused transport |
| Set cue comment/name | Correlated reply and complete UTF-8 field update; all other stored fields preserved |
| Set cue color | Correlated reply and native RGB/palette update; all other stored fields preserved |

Firmware 2.00 has passed all 26 command behaviors on logical Decks 1–4,
all 104 stale-load refusals, time/beat loop scaling, variable-tempo interpolation,
minimum-clamp uncertainty, Active Loop switching and already-engaged ENTER/RELOOP
uncertainty. The current candidate's persisted Memory point recall also passed
with a source-bound seek, actual paused transport and the full collection intact.

Firmware 1.30 has passed all 26 behaviors on Decks 1–4, all 104 stale-load
refusals, variable-tempo loop scaling, minimum-clamp uncertainty, Active Loop
enable/switch/disable and already-engaged ENTER/RELOOP uncertainty. The switch
completed in 3.10 seconds under its original 10-second deadline; both disposable
loops were removed and the original collection restored.

Both profiles passed graceful cold-boot readback on the same complete writable
disk pair: Memory UTF-8/palette metadata, a saved Active Loop, a Hot loop with
UTF-8/RGB, and all original fixture-baseline cues. A later cold boot verified
Memory/Saved deletion and Hot clearing. Firmware 1.30's initial fixture setup
cleared occupied Hot H on its disposable overlay; the raw base was unchanged.
The 1.30 storage run retained an earlier Save deadline failure; selected Save
subsequently passed in 2.98 seconds without a production Save change or deadline
increase. A passing later run does not erase that earlier uncertainty.

These results cover the opt-in native command path through
`core->library_service(1)`. The generic command adapter remains disabled and the
production ownership gate remains required. The checked candidate uses azmod
`342311f78f86...` and azstem `cbc2df3513ae...`, preserving 108/47 exports and the
public header. The earlier 2.00 matrix also includes unchanged loop/cue paths
from the preceding candidate, with the final Memory recall correction tested
separately. Final SDK command/probe/provider/harness/header checks pass (19
tests), and the public header is byte-identical to Core's. Physical command
acceptance has not been performed in this continuation. Missing broader
live/source APIs below remain follow-up work.

## Validation and release gaps

Synthetic tests cover parser bounds, time-zero cues, hot/memory loops, comments/colors,
empty versus absent data, fractional/variable-tempo conversion, missing meter, invalid rates,
source-qualified cache keys, sharing, owner quotas, stale/released tickets, cancellation,
journal overflow and concurrent consumers. The disk provider test covers real read-only
SQLite rows, fake physical USB ports, mount changes, file creation invalidation and path
containment, database symlink rejection and live-WAL rejection. A deliberately broken source identity is required to fail the cache test.

Broader delivery gaps: native source qualification for loaded decks, loaded native
analysis preference, native edit notifications, full live state, native/file
comparison and hardware latency, event freshness and long-running tests. The
command acceptance scope and retained limitations are stated above.

The frozen SDK v2 packet passes 99 standalone runs and 145 diagnostic controls
on each of the 1.30 and 2.00 ARM kernels. Its 73 compiled inputs and coherent
full/stripped core pair are bound by immutable manifests. These standalone results
include synthetic ownership and ABI fixtures; they do not establish source-qualified
native loads or asynchronous command support.

Both profiles now load the genuine Bad Romance export on all four decks, with
fresh loaded heartbeat/title observations and one LOAD per deck. Global Quantize
round trips pass on all four. A separate 2.00 run captures genuine first Play and
KeyLock receipts on every deck, then passes a known-state raw Play round trip on
deck 1. Its later controller failure came from the 900-second panel receiver
expiry; native process exit was not demonstrated. Later public v12 acceptance
passes Master Tempo and Sync on all four decks of both profiles after the normal
Allow/apply flow and a newly pinned process. Each stream records newer same-unit
native/public revisions and controlled values1→0→1. These are unit settings;
paused motion, loaded source association and the remaining controls stay open.

Later loaded runs on both profiles independently pass genuine raw Play and
KeyLock known-state changes on all four decks. Position/motion qualification
remains incomplete on some decks; those receipts do not establish normalized
paused state, source rate or load identity. No manual paused-loop stimulus is
sent without the independent witness.

The newer private source dependency batch passes source connection-incarnation,
owner isolation, cache reclamation and actual message-hook ordering tests in
host sanitizer and ARM suites. Canonical lifecycle forwarding also passes
against both exact SQLCipher modules using disposable memory-only connections.
The connection installer now has a startup caller; its current runtime acceptance remains separate. The historical723-file
core link/export/strip/lint passes; fresh packet and runtime acceptance remain pending. These historical source-dependency results do not qualify the immutable v2
runtime package. Current command acceptance is scoped separately above.

The correctly symbol-bound v2 source probe observes a current SQLite source and
physical metadata on 2.00, with zero invalid records in the retained six scans.
`loaded_source_valid` remains zero. An older source diagnostic called the wrong
private function and is retained separately with that classification. Exact v2
inputs and results are under `anchor/output/sdk-feature-goal-20261028` and
`anchor/output/sdk-emulator-goal-20261028`; no stale build reuse qualifies them.

Physical deployment/reload requires a requested physical test stage with the deck available for reload. No physical target write or control occurred in this historical feature batch.

A candidate maintainer handoff now contains20 native cache entry pairs and4
excluded/optional boundaries with both-build original-byte/FDE evidence. Generated
tables remain unchanged pending the private generator workflow. These values do
not enable native hooks or complete ownership/client-to-load qualification. Never
add zero entries, guessed addresses or hand-edited generated arrays.

The supplied real DAT/EXT pair also passes an independent raw-field comparison: 753 beats,
16 point cues, 20 phrases and 400 overview bytes. A PCO2-stripped in-memory control exposed
and now verifies the split legacy hot-cue fallback fix. See
[ANLZ evidence](15-analysis-evidence.md) and [native research](16-native-evidence.md).
The original track has no saved loops. A second analysis-only pair verifies two saved
hot-cue loops, their extended and legacy boundaries, and two boundary-error controls. Real-file
saved Memory loop parsing and native/file comparison remain unverified; native
command Active Loop persistence is covered separately above. Standalone parser/cache,
disk provider and real-file probes pass plain, ASan/UBSan and TSan on the authorized 2.00
deck; these checks do not establish native in-process SDK acceptance.

Additional stored-data regressions independently verify eight raw memory palette IDs,
PWV4/PWV6 start/middle/tail windows against the original files, optional short PCP2 tails,
recognized saved Active Loop statuses and exact counterpart matching. PSSI v1 has primary
format and synthetic/derived coverage; a natural export remains pending. The standalone
stored suite checks parser/provider bounds and catches deliberate regressions, including
the preview clock, width/header/capacity and duration validity. These file checks establish
neither native loaded-analysis precedence nor calibrated rendering. See
stored completion evidence in the core checkout.

The expanded native-kernel run passes 27 standalone checks in plain, ASan/UBSan (leak
detection enabled) and TSan modes, with fourteen deliberately broken controls detected.
This includes packed color windows, the working-boundary decoder and v1/v2 export
compatibility. EP147 keeps its PID; corrected in-process loop behavior is still unverified.

The corrected engagement predicate now follows playing loop/EXIT/RELOOP on physical
logical deck 1 of the 2.00 deck, with no automatic restart. Bounds remain unavailable in
that same run. This is a limited engagement result; other decks, paused cases and 1.30
native runtime behavior remain open. The unified inspector is confirmed loaded through
the shared ABI-1 service and displays real USB cues, beats, phrases and detail-window timing.
