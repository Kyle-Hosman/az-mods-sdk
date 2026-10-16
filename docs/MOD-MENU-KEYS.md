# Mod Menu keys

Every official Mod Menu setting has a key: its name in the settings file on the USB,
`MODS/azmods.cfg`, which is also the name your mod passes to `row_lookup()` to read it.
Keys are a stable interface: a key is never renamed or reused, and a removed setting keeps
its key reserved.

- `row_lookup(key)` returns a read-only handle, and `row_value(handle)` gives the value
  below. A toggle reads `1` only while it and every toggle above it are on.
- `row_set()` refuses official rows: only the user can change them.
- Groups (`fps60`, `extras`) hold only other rows and have no value of their own.
- Your mod's own rows are saved beside these as `<your mod id>:<key>`.

```c
static int h_waveforms = -1;

/* in init(): */
static void find_rows(void)
{
    h_waveforms = C->row_lookup("fps60-waveforms");
}

/* anywhere: 1 = on, 0 = off, -1 = no such row on this core */
static int smooth_waveforms(void)
{
    return h_waveforms < 0 ? -1 : C->row_value(h_waveforms);
}
```

<!-- keys:begin (generated from features.def by keys_test.c --print, in the unit tests) -->
| key | label | kind | parent | values |
|---|---|---|---|---|
| `stems` | Stems | toggle | - | 0 = off, 1 = on |
| `stem-pads-page` | Stems on BEAT LOOP 2 | choice | stems | 0 = "BEAT LOOP", 1 = "BEAT LOOP 2" |
| `remote-stems` | Remote Stems | toggle | stems | 0 = off, 1 = on |
| `stem-marks` | Stem Marks | toggle | stems | 0 = off, 1 = on |
| `stem-mark-style` | Stem Mark Style | choice | stem-marks | 0 = "Color", 1 = "Mono" |
| `stem-volume-eq` | Stem Volume on CH 3/4 EQ | toggle | stems | 0 = off, 1 = on |
| `drum-roll` | Drum Roll | toggle | - | 0 = off, 1 = on |
| `drum-roll-bank` | Bank | choice | drum-roll | 0 = "1", 1 = "2", 2 = "3", 3 = "4" |
| `fps60-waveforms` | 60 FPS Waveforms | toggle | fps60 | 0 = off, 1 = on |
| `fps60-ui` | 60 FPS UI | toggle | fps60 | 0 = off, 1 = on |
| `playlist-editing` | Playlist Editing | toggle | - | 0 = off, 1 = on |
| `playlist-grab` | SHIFT + Hold to Move | choice | playlist-editing | 0 = "PRESS", 1 = "SHIFT + PRESS" |
| `themes` | Themes | toggle | - | 0 = off, 1 = on |
| `overlays` | Overlays | toggle | extras | 0 = off, 1 = on |
| `overlay-fps` | FPS | toggle | overlays | 0 = off, 1 = on |
| `overlay-cpu-temp` | CPU Temp | toggle | overlays | 0 = off, 1 = on |
| `overlay-cpu-usage` | CPU Usage | toggle | overlays | 0 = off, 1 = on |
| `overlay-cpu-clock` | CPU Clock | toggle | overlays | 0 = off, 1 = on |
| `overlay-ram` | RAM Usage | toggle | overlays | 0 = off, 1 = on |
| `beat-jump-2-first` | BEAT JUMP 2 First | toggle | extras | 0 = off, 1 = on |
| `fine-vinyl-speed` | Fine Vinyl Speed Adjust | toggle | extras | 0 = off, 1 = on |
| `bt-auto-connect` | Bluetooth Auto-Connect | toggle | extras | 0 = off, 1 = on |
| `bt-remember-channel` | Remember Selected Channel | toggle | extras | 0 = off, 1 = on |
| `hide-track-title` | Hide Track Title | toggle | extras | 0 = off, 1 = on |
| `phase-meter` | Phase Meter | toggle | extras | 0 = off, 1 = on |
<!-- keys:end -->
