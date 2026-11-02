# Core library service development contract

This shared core service implements the portable foundation of the Core SDK implementation plan.
It is experimental and is off by default. To enable it on a development deck, create
`/home/root/settings/azmod.track-data.on` before restarting EP147. Removing the flag and
restarting disables the new provider. No generated address tables were changed.

The current milestone is completing the existing native loop/cue commands through
behavioral tests. All 26 IDs have development paths; broader live-state/source
features remain follow-up work. Waveform processing and PRO DJ LINK are out of scope.
The support matrix in [CORE-SDK-SUPPORT.md](CORE-SDK-SUPPORT.md) records those gaps.

## Compatibility

`az_core_v1` only grows at its end. Existing fields and `AZ_MOD_ABI` are unchanged. The core
and SDK public headers are identical. `AZ_HAS(core, field)` is required for every new entry;
then check `data_capabilities` and the individual component/field validity. The development
baseline is core 1.2.3. A release minimum must be assigned when these services are released;
setting `min_core` to stock 1.2.3 alone does not establish service availability.

New v1 output structures require at least their complete v1 size, except `deck_snapshot`
also accepts its earlier development prefix through `main_cue`. Too-small output buffers
are rejected without writes. Larger buffers keep their unknown tails and their caller-set
size. Input references and requests require version `AZ_DATA_VERSION`. The immutable
snapshot includes its own size and version. The old `azstem_deck_snapshot` export and its
exact size check are unchanged. New loaded observations use a separate versioned export,
`azstem_deck_observation_v1`; loop notifications have another independent versioned export.
Mixed library versions report absent fields unavailable.

## Identity and stored data

A mod worker calls `track_resolve(id, stick, content_id, &ref)` using an ID obtained from that
stick's library. This produces a source-qualified reference without loading a deck. Resolution
establishes the source connection; a nonexistent content row is rejected by the background
load. Stick numbers are physical USB ports 0 and 1, resolved by the existing sysfs mechanism.
Ambiguous ports, escaped mount paths, hubs with unknown ports and absent libraries fail.

`source_id` identifies a port in this core session. `source_generation` changes when the
observed mount identity, database inode or connection changes. The provider checks Linux's
mount ID as well as the mount path, so a reconnect between polls with a different mount ID
is detected. Mount IDs and stat fingerprints are operating-system identity/freshness signals,
not cryptographic identities. The rare reuse of every identity input without an intervening
observation cannot be detected. `library_id` identifies the database inode in its connection;
it is not rekordbox's permanent library UUID. References must not be persisted across restarts.
Duplicate content IDs on different sources are distinct cache keys.

`track_request` queues a component mask and returns a ticket. It does no file I/O. Multiple
consumers requesting the same reference and mask share one immutable result. Poll
`track_acquire`; `AZ_PENDING` means loading, `AZ_BUSY` means retry on another tick. `AZ_OK`
returns a borrowed immutable pointer. Copy small display values, then release the ticket.
All nested arrays have that pointer's lifetime. Do not edit them or retain them after release.

At most four tickets per mod and 32 in total pin up to eight snapshots. Tickets cannot be
used by another mod. Cancellation affects only that consumer; it does not interrupt another
consumer's read. Cancelled tickets still need release. Pointers already acquired remain
readable through cancellation or invalidation until release. Invalidation makes subsequent
acquisitions return `AZ_STALE`; it never frees pinned memory. Release a stale ticket and
resolve/request again. Repeated acquisition of one ticket does not add a pin.
If `init` fails, the core retires that mod's data owner and reclaims tickets that
were never acquired. Release acquired tickets before returning failure: pointers
already handed out stay pinned because the mod may have started a thread.

A single core worker owns allocations, file reads, parsing and eviction. Each file is limited
to 16 MiB; an ANLZ pair has at most 32 MiB of input, plus fewer than 8 MiB for eight decoded
snapshots. The parser caps beats at 32768, phrases at 1024, cues at 256 and overview bytes at
4096. Unpinned cache entries are evicted by recency. Cache operations use try-locks and return
`AZ_BUSY` instead of waiting. File paths are resolved inside the selected mount; the provider
rejects paths that escape through parent traversal or symlinks. Reads use close-on-exec file
descriptors. Files are not opened on playback, input or paint callbacks.

The worker checks source and file fingerprints before publication and on subsequent sweeps.
The database and both analysis filenames participate, including a missing EXT that later
appears. Original source-relative analysis paths are checked alongside their resolved targets,
so retargeting an in-source symlink invalidates a pinned snapshot without freeing its memory.
Analysis requests reject database paths that exceed the provider's 1023-byte limit with
`AZ_LIMIT`; metadata-only requests remain independent of that limit. Analysis readers open
without waiting for special-file peers and reject nonregular files before reading them.
Metadata-only requests do not read ANLZ files. File changes during a read cause a
stale result rather than a mixed result. A nonempty WAL or rollback journal makes the new
provider unavailable/stale: immutable reads must not silently ignore committed sidecar data.
The database itself, as well as analysis paths, must resolve inside its source mount. Fingerprints are best-effort observations, so they do
not replace native edit notifications. There is no hard freshness deadline: large file reads
or slow media delay sweeps. `AZ_DECK_CUES_CHANGED` is a separate native cue-set
invalidation event delivered by `on_deck_event` on the UI thread when the native-live
observer is enabled. Pending changes are coalesced per owned player registration;
retirement invalidates its previous receipt. The event includes load/reset changes
and does not establish a save, cue classification, or source/load identity. Saved-file
cache invalidation and loaded-data precedence remain future work. Saved file data
never claims to reflect unsaved native edits.

After collection, `deck_snapshot` exposes `cue_unit_generation`, `cue_revision`
and `cue_observed_ns` under `AZ_LIVE_CUE_REVISION`. These are the cue listener's
registration lifetime, core collection counter and monotonic GUI collection time.
The original loaded-track `revision` is independent. Polling a snapshot does not
drain native changes or advance the cue counter; a pending callback becomes
observable after a GUI tick. Retirement or disabling the observer clears validity.
Check the caller's snapshot size before reading these appended fields.
The cue bit in `data_capabilities.live_fields` reports active observer registration
independently of whether its first receipt has arrived. Register during `init` and
check the snapshot's validity for current data when a change arrives.

## Availability, provenance and timing

An absent tag is unsupported, an explicit zero-entry tag is empty, a pending ticket is
loading, and malformed/read failures return an error. A failed result exposes no partial
snapshot. Every parsed component has a revision and observation time. These describe
publication of saved data, not the exact instant a native user edited a track.

PCO2 lists take precedence over PCOB independently for hot and memory cues, including an
explicit empty extended list. Legacy hot PCOB lists merge across DAT and EXT by slot;
legacy memory lists use DAT when that classification exists, otherwise EXT. Memory ordering
is saved-file ordering, not a verified native UI ordering. Time zero is valid. Cue point versus loop comes from the entry
type. A saved loop does not imply engagement. Color and comment have separate validity bits.
`AZ_CUE_COLOR` describes optional hot-cue RGB; `AZ_CUE_COLOR_ID` describes the raw saved
memory palette ID, including zero, in `color_id`. Its union alias `reserved` preserves the
entry stride. Memory trailer bytes are not RGB, and no native palette is inferred.
PCPT loop statuses 0/1 expose saved `auto_active = 0`, and status 4 exposes 1, under
`AZ_CUE_AUTO_ACTIVE`. PCP2 can receive this property only from one exact, unambiguous legacy
loop counterpart matching classification, type, start/end and hot slot when applicable.
Unknown statuses, duplicates and mismatches leave activation unavailable. Native automatic
activation behavior remains unverified. Hot entry identity is its slot (1 through 8); memory entry
identity is scoped to that cue revision and may change after an edit or reload. Main cue,
in-memory modifications and associations with the live loop are unavailable.

PQTZ preserves every beat's original time, tempo and beat-in-bar value. No downbeat is
invented for a zero value. PSSI versions 0/1 preserve raw kind and mood; normalized category
remains unknown. Version and stride are separate 16-bit fields. An unknown version marks
phrases unsupported while other valid requested components remain usable; malformed lengths
still fail the read. Version 1 has documented/synthetic coverage and awaits a natural export.
Beat positions are zero-based fractional indexes. Timing helpers interpolate
between adjacent grid times and reject extrapolation. They therefore handle changing tempo
and off-grid interval boundaries. Source seconds and beat length are separate from estimated
playback seconds at the caller's explicit positive rate. A zero rate leaves playback duration
unavailable. Bar length remains unavailable because no reliable meter model is present.

PWAV returns its saved overview height bytes, not RGB or PCM. The overview covers the track
and its approximate seconds-per-sample uses the library duration divided by sample count.
The database duration is rounded to seconds; this is not precise waveform timing. Without a
usable duration the overview waveform is unavailable. Raw mono/RGB/three-band detail and
all previews use the separate bounded-window service below.

Master Tempo is available under `AZ_LIVE_MASTER_TEMPO` after a valid native KeyLock
receipt. Both admitted firmware builds register a `MasterTempoSetter` that maps this
receipt to the MasterTempo setting and maps the setting back to `setMasterTempo(bool)`.
`master_tempo` is 0/1, or -1 without validity; its receipt identity/time are
`live_unit_generation`, `key_lock_revision` and `key_lock_observed_ns`. This independent
setting does not establish source/load identity or audible time-stretch behavior.

`AZ_LIVE_SYNC` describes the independent Sync enabled setting: native mode0 is off,
mode2 is on; mode1 and other values remain unavailable. Without validity, `sync`
is-1. The appended `sync_raw_mode`, `sync_unit_generation`, `sync_revision` and
`sync_observed_ns` identify that receipt, including an invalid observed mode.
This setting does not establish beat lock, master role or a track load. Its
private export is separate, preserving the fixed live-v1 ABI.

## One shared library service

Obtain `const az_library_v1 *library = core->library_service(AZ_LIBRARY_ABI)` after checking
`AZ_HAS(core, library_service)`. An unsupported ABI returns NULL. Check individual members
with `AZ_LIB_HAS(library, member)` before calling them. The table stays valid for the process
lifetime; track and waveform snapshots retain their separate ticket lifetimes.

This one core-owned service provides library enumeration, source qualification, stored
analysis, live snapshots, timing, events and command submission. Every consumer shares the
same bounded cache, ticket quotas and source lifecycle. Native observation crosses the
existing internal azstem bridge; consumers require no private firmware addresses or hooks.
The individual development entries in `az_core_v1` remain compatibility aliases to the same
functions and state. New clients use the single versioned service table.

`examples/library-inspector` is one diagnostic client with Cues, Live, Analysis and Control
pages. It implements display and input only; the functionality remains in the core service.
It replaces the four separate demonstration mods. Command buttons still follow capabilities,
so an unavailable native operation performs no action.

The [wave-layer probe](../examples/wave-layer-probe/README.md) exercises raw PWV7 and
derived layered windows through this public service from a managed worker. It selects
public USB1 port0, resolves a returned library track, checks payload reconstruction,
provenance, reserved bytes and window bounds, and releases its tickets. Its structured
results support controlled normal-loader acceptance of encoding8.

```c
static const az_library_v1 *get_library_service(const az_core_v1 *core)
{
    if (!AZ_HAS(core, library_service)) return NULL;
    const az_library_v1 *library = core->library_service(AZ_LIBRARY_ABI);
    return AZ_LIB_HAS(library, data_capabilities) ? library : NULL;
}
```

## Detailed waveform windows

`waveform_request(id, track, query, &ticket)` and `waveform_acquire(id, ticket, &view)`
provide canonical detail/preview encodings and a derived layered format from local USB analysis. Check both entries
with `AZ_LIB_HAS`. The query names `AZ_WAVE_PWV3_MONO` (1), `AZ_WAVE_PWV5_RGB16` (2),
`AZ_WAVE_PWV7_BANDS` (3), `AZ_WAVE_PWV4_PREVIEW` (4), `AZ_WAVE_PWV6_PREVIEW` (5),
`AZ_WAVE_PWV2_TINY_PREVIEW` (6), `AZ_WAVE_PWAV_PREVIEW` (7) or `AZ_WAVE_PWV7_LAYERED_RAW` (8), a zero-based `first_column` and a positive
`column_count` of at most `AZ_WAVE_WINDOW_MAX` (4096). The last window is clipped to the
stored extent. For nonempty data a first column at or beyond the end is invalid; a valid
zero-column tag accepts only first zero and publishes `AZ_AVAIL_EMPTY`.

PWV3 uses one byte per column: height in bits0–4 and intensity in bits5–7. PWV5 uses
a big-endian16-bit word with three-bit red/green/blue fields starting at bits13/10/7 and
five-bit height at bits2–6; the low two bits are preserved without interpretation. PWV7
uses three uncalibrated bytes per column, ordered low/mid/high bands. `bytes_per_column`
is1/2/3 respectively; `data_bytes` is the returned column count times that stride. The source axis is
150 columns per second from origin zero: `start_s = first_column / 150.0`,
`seconds_per_column = 1 / 150.0`. This fixed analysis clock is independent of integer-second
library duration and pitched playback. It does not promise exact decoder, DAC or UI alignment.
These are lossless stored encodings. Native display gains, gamma, exact frequency cutoffs
and faithful player rendering remain unavailable. Unfamiliar variants return unsupported.

`AZ_WAVE_PWV7_LAYERED_RAW` derives six-byte columns from the canonical PWV7 data.
Bytes 0–1 contain a little-endian header: bits 12–15 hold the layer count (1–3);
the three low nibbles hold successive symbolic layer IDs. Bytes 2–4 hold the
corresponding absolute raw magnitudes in descending order, with unused ID and
magnitude fields zero. IDs 1/2/3/4/5/6/8 mean C/B/A/BC/AC/AB/ABC, where A/B/C
label PWV7 input bytes 0/1/2. Allzero input produces one ID 8 layer with magnitude 0.
Byte 5 is SDK-reserved zero; it does not expose the undefined padding in native
allocations. This format uses the same 150-column/s axis and
`info.provenance == AZ_PROV_DERIVED`. It supplies neither RGB values nor gain,
gamma, frequency cutoffs or player pixels. Raw encoding 3 remains available.
An older core can reject encoding 8 with `AZ_UNSUPPORTED`; check that result
before choosing a supported fallback.

PWV4 previews preserve six bytes per column; PWV6 previews preserve three. PWV2 tiny previews
and PWAV previews preserve one raw byte per column. The latter two use a 20-byte header
with count at +12 and the recognized opaque word `0x00010000` at +16; that word is not
interpreted as gain or a sample rate. These previews cover the
whole track and use the approximate axis `duration_s / total_columns`, derived from valid
positive finite library duration. They never inherit the detail clock of 150 Hz. Without
usable duration the preview request is unsupported. This time axis is rounded with the
database's duration, and these raw preview encodings have no native gain/pixel calibration.
Zero-entry tags publish empty when duration is usable. Query size/version/reserved/count
validation happens before queuing: zero count is invalid, count above 4096 is limited and
unknown encoding IDs are unsupported. A nonempty out-of-range first column fails the read.

PWAV/PWV2 are selected by tag in DAT, PWV3/PWV4/PWV5 in the sibling EXT container, and PWV6/PWV7 in 2EX. The derived layered format also reads PWV7 from 2EX; a file name
alone never selects its wire representation. The normal-priority provider streams the bounded PMAI/tag headers and reads only the selected
sample span. It validates later tag envelopes and rejects duplicate requested tags. An unfamiliar
canonical-header word or stride is unsupported; malformed lengths fail. Database, source and
analysis-file fingerprints are checked around publication and during freshness sweeps.

Windows share the eight cache slots and four tickets per mod with ordinary track snapshots.
Track and waveform ticket identifiers never wrap. Exhausting their identity counter
returns `AZ_LIMIT` without changing the output ticket or existing acquisitions.
Snapshot publication also reports `AZ_LIMIT` when no new revision identity can be
assigned; previously acquired snapshots stay valid until released.
The cache key includes encoding, first column and requested count. `track_release`,
`track_cancel` and `data_event` apply to both ticket kinds. Calling the wrong acquire function
returns `AZ_INVALID`. Arrays remain immutable and pinned until release even after invalidation.
Events identify the waveform component and ticket; asynchronous failures use the existing
status codes. No file work occurs in a draw, playback or native notification callback.

The library inspector's Analysis page shows a selected bounded stored waveform when available, preserving the
raw overview fallback for an older core. The representation is stored analysis and does not
claim calibrated player colors or audio amplitude. Format and timing provenance comes from
[primary ANLZ research](https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/anlz.html#waveform-detail)
and [the primary WaveformDetail implementation](https://github.com/Deep-Symmetry/beat-link/blob/main/src/main/java/org/deepsymmetry/beatlink/data/WaveformDetail.java),
corroborated against the supplied files.

## Observations, events and commands

`deck_snapshot` exposes authoritative loaded/empty state from the copied
LoadedTrackInfo, together with the capture time and its observation revision. A sequence-lock
collision yields no valid fields and can be retried. A capture revision is **not** a load
generation. The new API does not expose a playhead without a load association, or treat the
legacy forward-movement heuristic as paused/playing state.

A separate experimental listener is enabled only when both the track-data flag and
`/home/root/settings/azmod.native-loop.on` exist before startup and the existing player
constructor/destructor hooks both installed at their expected entries. It registers through
the owned native notification server's virtual interface, with both-build layout/RTTI guards
and GNU `std::weak_ptr` ownership. No additional absolute hook address is introduced.
The isolated C++ translation unit requires GNU libstdc++, which EP147 already uses.

When available, `AZ_LIVE_LOOP_ENGAGED` and `loop.valid & AZ_LOOP_ENGAGED` describe the
native notification's current working-loop validity. Registration may be asynchronous and
initial replay is unverified, so a registered capability can still have no valid observation.
The listener applies the same five scalar validity checks as native `isActiveLoopPlay`.
EXIT clears this working property while retained cue slots remain separate. The leading
notification byte describes a different mode and is ignored; hardware testing exposed the
earlier incorrect interpretation.

`AZ_LIVE_LOOP_BOUNDS` reports support for current working IN/OUT coordinates. Check
`loop.valid & AZ_LOOP_BOUNDS` on each observation before reading `in_s` or `out_s`.
Both are source seconds; zero IN is valid. Availability requires both native position
predicates, matching source tuples and borrowed source associations, positive equal Hertz,
forward nonnegative sample positions, verified native class identities and consistent cached
microseconds. The inspected native sample-first setters round cached microseconds to nearest,
ties away from zero. SDK consistency allows one sample at the record's Hertz plus half a
microsecond of cache rounding. It compares `2 * abs(samples * 1000000 - cached_us * Hertz)`
with `2000000 + Hertz` using wide integer arithmetic, separately for IN and OUT. This is a
resolution-aware SDK policy, not a claim that every native writer uses the same conversion.
Published seconds always come from samples/Hertz; the cache never replaces or snaps them.
A fixed millisecond tolerance is not used. An engaged loop may temporarily have unavailable bounds. EXIT invalidates current bounds; it does not
establish whether retained points exist. Defined state, reloop availability, mode and saved
association remain invalid. No native pointer or owning cue property is retained.

The private `azstem_loop_observation_v2` export adds bounds and certifies this decoder.
The original 40-byte v1 export stays compatible. New azmod requires v2, leaving loop fields
unavailable with an older experimental azstem rather than trusting its superseded reader.

`loop_observed_ns` is the core callback's receipt time, not the native source capture time.
`loop_revision` counts core publications and `loop_unit_generation` identifies a player-unit
registration. Neither is a track-load generation. Loaded and loop observations are separate
streams; the loop notification does not establish a source-qualified track association.
Native callbacks from retired units cannot publish into a replacement generation. The new
listener's runtime validation is still in progress; the flag is for controlled development,
not a claim of completed native firmware/hardware acceptance.

A separate development listener requires both the track-data flag and
`/home/root/settings/azmod.native-live.on` before startup. It publishes raw play notification
flags (`AZ_LIVE_PLAY_FLAGS`), quantize (`AZ_LIVE_QUANTIZE`) and raw key lock
(`AZ_LIVE_KEY_LOCK`) through checked weak native interfaces. Inspect each validity bit
before reading its values. `live_unit_generation` names the player registration;
`play_revision/play_observed_ns`, `quantize_revision/quantize_observed_ns` and
`key_lock_revision/key_lock_observed_ns` are independent receipt streams. They do not
establish a loaded-track or source association.

`play_mode_is_2`, `play_pause` and `play_aux` preserve the three native bytes without naming
paused, playing or cueing semantics. The native notification is triggered by a change to
`play_pause` only; mode and auxiliary flags are context and can change without a notification.
Normalized `AZ_LIVE_PLAYBACK` remains unavailable. A strict 0/1 KeyLock receipt also qualifies
the Master Tempo setting described above. Invalid raw scalars are -1. No initial replay is
promised; registration can succeed before the first valid receipt. The private
`azstem_live_observation_v1` export and public
projection preserve independent validity and caller buffer sizes.

`AZ_LIVE_TEMPO` exposes the native GUI's tempo-adjusted BPM in `tempo`, using
the existing conservative range `20 < BPM < 400`. It is neither pitch percent
nor playback rate. The GUI polls an owned `BpmState` and publishes a copied
receipt about every250ms; public readers never dereference the native object or
wait for the publisher. `tempo_state_generation`, `tempo_revision` and
`tempo_observed_ns` identify that BPM-state observation. A poll that finds
invalid or retired native state clears validity; a contended public read or
unobserved state leaves the bit clear and `tempo` zero. Inspect the receipt age:
the last copied value remains observable until the next successful poll. This
state generation is independent of player-unit and track-load generations.

`AZ_LIVE_TEMPO_SLIDER_LOCKED` supplies the separate native slider-lock setting
in `tempo_slider_locked` (0/1, otherwise -1). Its independent receipts use
`tempo_lock_unit_generation`, `tempo_lock_revision` and `tempo_lock_observed_ns`.
It requires the native-live observer flag, has no initial replay, and does not
establish confirmed master selection, playback rate or track identity.

`AZ_LIVE_NATIVE_LOAD` exposes `native_load_generation` from a successful native
completion matched to its observed request. `native_load_unit_generation`,
`native_load_invalidation_epoch` and `native_load_observed_ns` identify that
independent receipt. A same-track reload gets a new generation; pending,
failed, unloaded or retired state has no valid native generation. This requires
the native-load observer opt-in. It leaves `load_generation` and `track`
unavailable: it does not qualify a USB/library source, join LTI metadata or
supply a source-qualified command target. Transient loop commands explicitly accept this
generation with `AZ_COMMAND_NATIVE_LOAD`. Check the validity bit and
compare streams only using identities they actually share.

`AZ_LIVE_MASTER` reports the confirmed local master from the existing native
post-exchange event. Every deck snapshot shares `master_revision` and
`master_observed_ns`; `master` is1 for the selected local deck and0 for the
other three. Before a confirmation, or after a malformed/foreign member, bad
clock or contended publication, the bit is clear and `master` is-1. Readers
never wait for a callback. This event is independent of Sync enablement, a
master-change request, playback and track-load identity.

`data_event` polls a 64-entry ordered journal, scoped to the calling mod. Publication occurs
under the same lock after the snapshot becomes acquirable. Sequence and revision numbers
correlate results with tickets. Poll from a mod tick or worker and keep the last sequence.
`AZ_EMPTY` means no newer event; `missed=1` means refresh outstanding state. Journal
sequence exhaustion returns `AZ_LIMIT` after the last available cursor; it never
wraps or reuses a cursor. Existing tickets remain usable. The journal may
coalesce history by dropping old entries. There are no callbacks, guaranteed beat events or
loop-iteration events. Deck observation and command events are reserved in the contract but
are not emitted yet. Poll deck state directly.

Native cue commands now have development implementations under
`azmod.native-cue-commands.on`. MAIN_SET/RECALL and HOT_TRIGGER use the native
control path; saved/hot/memory writes and metadata changes use the native cue
actor and persistence queues. Read `native_cues` for revision-scoped native IDs.
Selected operations require `AZ_COMMAND_NATIVE_CUE_IDS`; saved writes require
`AZ_COMMAND_PERSIST`. Use native quantization and a valid native load generation.
These are implementation paths, not a claim that every firmware/deck runtime test
has passed. Test actual seek/loop/transport effects and saved reload/restart behavior.

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
Cancellation after dispatch cannot undo an audible operation. Firmware/hardware
acceptance remains pending; static mappings and host fixtures do not establish
deck execution.

`command_submit` validates reserved bits, finite numbers and terminated comments.
Other operations return `AZ_UNSUPPORTED` with a correlated rejected result;
malformed requests return `AZ_INVALID` with output unchanged. Poll/cancel/release
authenticate mod ownership and return `AZ_STALE` for unknown requests. A refused
submission creates no retained request. Cancellation before native execution
prevents the call; after the execution claim it may return pending.

The core now has one bounded command store and a separately tested policy engine for
per-deck ordering, exact retry comparison, owner quotas, deadlines, cancellation and
immutable terminal outcomes. Accepted IDs must increase per mod and cannot be reused after
release. An in-flight timeout remains uncertain and keeps its deck fenced until a trusted
adapter proves execution settled. These mechanisms have synthetic adapter and regression
coverage. Transient loop commands add native asynchronous execution, descendant lifetime tracking
and checked operation postconditions. Native cue/persistence implementations need their own
behavioral, reload/restart and firmware/deck tests. Physical-control ordering remains unverified. Check service size before using appended
entries and check command capabilities before submitting.

Native support must first supply state readers, owning-thread dispatch, exact signatures,
object lifetime rules and build mappings for every advertised operation. Required table
entries need both build mappings; optional features use the existing optional-feature pattern.
The core's `tools/ep147-addrgen.py` validates reviewed mappings against both
exact stock firmwares and regenerates the address tables. Update its mapping
catalog and run generation/check mode; never hand-edit generated tables.

## Local real-file probe

With a user-owned ANLZ sample directory, run `ANLZ_SAMPLE_DIR=/path/to/samples SAN=asan tests/sdk-data.sh` on one shell line. The optional probe also runs under TSan. It
keeps all original and derived bytes outside the contribution. The measured results and
limitations are in [ANLZ evidence](CORE-SDK-ANLZ-EVIDENCE.md); native research is recorded in
[native evidence](CORE-SDK-NATIVE-EVIDENCE.md).

`tests/sdk-stored.sh` separately covers optional PCP2 tails, saved status matching, raw
memory color IDs, PSSI versions and bounded PWV4/PWV6 windows, with deliberate regression
controls. With the original sample it independently compares eight memory IDs and three
streamed windows per preview encoding against raw file bytes. It also runs without assets.
See [stored-data completion evidence](CORE-SDK-STORED-COMPLETION.md) for the exact scope and
remaining real saved-memory/native tests; waveform calibration is outside the current scope.

## Timing precision and invalid clocks

Use `compare_timestamps()` from `azmod-timestamps.h` for every timestamp comparison,
including equality, ordering, ranges and deadlines:

```c
int order = compare_timestamps(first, second, units_per_second, sample_rate, window_samples);
if (order == AZ_TIME_EQUAL) { /* equivalent within the actual sample window */ }
```

The function returns `AZ_TIME_BEFORE`, `AZ_TIME_EQUAL`, `AZ_TIME_AFTER`, or
`AZ_TIME_INVALID` for invalid parameters/non-finite floating timestamps. Equality means
`abs(second - first) < window_samples / sample_rate` seconds. A difference exactly at
the window boundary is ordered. Equivalence works across fixed bucket boundaries and
is symmetric, but is not transitive; do not use it as a sorting predicate.

Both timestamps must use the same clock and units. For source seconds, pass units `1`
and the actual source rate and native processing window. For source samples, pass the
actual rate for both units and sample rate. Monotonic nanoseconds use their own clock
resolution (units/rate `1000000000`, window `1`); convert clock domains explicitly.
Source, load, request and cue identity checks still apply independently.

Native persistence serializes cached microseconds and reconstructs source samples.
Compare saved cues against that native round trip before applying the sample window;
do not widen the window to hide a format conversion. A reconstructed loop can own
separate IN and OUT source objects with the same source identity and rate.

Grid interpolation preserves the stored millisecond time axis and fractional beats.
It rejects non-finite, window-equivalent or reversed selected grid endpoints without
modifying the output. Stored analysis, native samples and receipt clocks each retain
their own rate and window.

A positive source interval whose playback-duration calculation underflows to zero leaves
`AZ_INTERVAL_PLAYBACK` unavailable. Endpoints equivalent within the stored time window
produce a valid zero source/playback duration. Failed or invalid monotonic clock reads yield an unavailable timestamp
of zero instead of uninitialized or wrapped nanoseconds.
