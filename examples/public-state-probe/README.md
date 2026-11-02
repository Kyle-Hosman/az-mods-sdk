# Public SDK state probe

This development mod reads Master Tempo, Sync, tempo-adjusted BPM and the tempo
slider-lock setting through the normal public SDK library service. Load and approve it through the normal
Community Mods flow. It uses no private symbols, native hooks or commands.

Run `python3 -m unittest discover -s tests -p test_public_state_probe.py` from the
SDK root, then `tools/azmod-pack build examples/public-state-probe` and
`tools/azmod-pack package examples/public-state-probe`.

The core-managed worker polls logical decks 1–4 once per second, with one public
snapshot call per deck. It logs changed records and repeats unchanged records
every ten polls. Both row types share one limit of512 data rows; collection also
stops after1,800 polls or30 monotonic minutes, whichever comes first. Clock and
format errors stop collection. The final record states the reason and count.
Reloading this mod starts a new bounded collection.

The existing `SDK_PUBLIC_STATE v=1` grammar is unchanged. Each prefix has its own
contiguous sequence numbers and completion count. Their data counts sum to at
most512; capability/completion lines are administrative. A core offering only the
older fields produces the original STATE-only output. TEMPO rows begin when new
capability bits or a new public field/receipt appears.

Each `SDK_PUBLIC_STATE v=1` record includes the queried one-based `deck`, returned
one-based `returned` deck, monotonic `query_ns`, API return code `rc`, validity
mask `valid`, and `mt`/`sync` values. A setting without its public validity bit is
reported as −1. `mt_unit`, `mt_rev` and `mt_ns` identify the native KeyLock receipt;
`sync_unit`, `sync_rev` and `sync_ns` identify the independent Sync receipt.
`raw` preserves an observed Sync raw mode even when that mode is unsupported.

Check `rc == AZ_OK`, matching queried/returned decks, the corresponding validity
bit, and a new native receipt revision/time after each controlled change.
`query_ns` is the time of the public API read; an unchanged native setting can
retain an older receipt time. These logs supplement the runtime freshness guard.
The initial capability line is a snapshot taken during mod initialization.

Master Tempo and Sync here describe enabled settings. These observations do not
establish transport state, beat lock, master role or a track/source association.
This read-only probe does not demonstrate native command completion.

The additional row has this exact field order:

```text
SDK_PUBLIC_TEMPO v=1 seq=N deck=D returned=R query_ns=Q rc=C valid=V tempo_valid=B tempo=DDD.dddddd tempo_state=G tempo_rev=I tempo_ns=T slider_valid=B slider_locked=L slider_unit=U slider_rev=J slider_ns=S
```

`deck` is one-based1–4. `returned` is the one-based returned deck, or0 when no
qualified response header is available. Integers are decimal; BPM has six decimal
places and a stable decimal point. `tempo_valid` and `slider_valid` are0/1 after
checking API success, record version/size, matching deck, the corresponding public
validity bit and a nonzero receipt identity/revision/time no later than `query_ns`.
Tempo additionally must be finite and inside the core's conservative20–400 BPM
range (endpoints excluded); slider lock must be exactly0 or1. Unknown values are
`tempo=0.000000` / `slider_locked=-1`. Missing/short tails never supply new fields.
The raw public validity mask and available receipt metadata remain visible even
when those qualification checks reject a scalar.

`tempo_state`, `tempo_rev`, `tempo_ns` copy the public BpmState lifetime and GUI
poll receipt. `slider_unit`, `slider_rev`, `slider_ns` copy the separate native
slider-lock unit receipt. BPM receipt revisions/times may advance every GUI poll
without a value change. Receipt-only increments do not trigger extra TEMPO rows;
first observation, availability/value/state-generation/slider changes do, along
with the ten-poll refresh carrying the latest clocks. Neither stream creates extra
snapshot calls, a new thread or a longer collection window.

The new completion line is `SDK_PUBLIC_TEMPO complete reason=REASON records=N`.
The STATE completion count still counts STATE rows alone, preserving its parser
contract. A format bound stops collection with `reason=format_limit`; TEMPO data
rows are shorter than512 bytes. These public observer tests do not validate native
semantics or runtime delivery. BPM is not playback rate/direction, and slider lock
does not establish master role, load/source identity, pause or command completion.

A core advertising confirmed master/native-load fields also emits:

```text
SDK_PUBLIC_NATIVE v=1 seq=N deck=D returned=R query_ns=Q rc=C valid=V master_valid=B master=M master_rev=I master_ns=T load_valid=B native_load=G load_unit=U load_epoch=E load_ns=S source_load=X tempo_valid=B tempo=DDD.dddddd tempo_state=G tempo_rev=I tempo_ns=T slider_valid=B slider_locked=L slider_unit=U slider_rev=J slider_ns=S
```

This prefix has its own sequence/footer and shares the existing512-row budget,
one snapshot call per deck, refresh cadence and collection deadline. A qualified
master is0/1 with its process event revision/time; unknown is−1. A qualified native
load has a nonzero generation, unit, invalidation epoch and receipt time. Each
qualification requires a successful matching-deck response, complete versioned
tail, public validity bit and receipt clock no later than the API query.
`source_load` independently reports the original source-qualified `load_generation`;
it must not be substituted for `native_load`. These rows do not join a load to a
track/source or authorize commands. A native-load observation can follow the
single ordinary LOAD already planned by an integration test; this probe sends no
controls and makes no pause, motion or playback-rate claim.

For loaded-deck integration, the compile-time `PROBE_NATIVE_ONLY=1` profile logs
only the combined NATIVE data rows. BPM/slider values use the same qualification
as TEMPO rows. This keeps the original512-row aggregate budget useful through
long navigation without increasing polling, snapshot calls, limits or deadlines.
The default build retains the separate STATE/TEMPO/NATIVE streams.
