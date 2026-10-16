# Logs

AZ-MODS's [docs/LOGS.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/LOGS.md) lists every log the deck
keeps, including the official mods' logs.

## The files

| File | Written | Limit | Kept |
|---|---|---|---|
| `/tmp/azmod-community/<id>.log` | the core's own lines about the mod (its load result, refused hooks, refused service calls), always; the mod's `log()` lines only while diagnostics are on | 256 KB ([`log`](API.md#log)) | until reboot (RAM) |
| `/tmp/azmod-community/loaded` | at load: `fw=<tag>`, `suspended=`, `off=`, then one `mod=<id>\|<state>\|<version>\|<reason>` line per mod | rewritten whole | until reboot |
| the `log_path` of a program started with `launch` | its stdout and stderr, copied by a core thread as they come | 1 MB, then moved to `<log>.1` | on the USB, in the mod's `data_dir` |
| systemd's journal, unit `azmod-handoff` | stdout and stderr of a program started with `handoff`, and the handoff guard's lines | the journal's | until reboot |
| `/tmp/azmod-crash.log` | one block per crash of EP147; `community mod: <id>` when the core blamed a mod | 256 KB, then `.1` | until reboot |
| `/home/root/settings/azmod-community/.blame`, `.initing` | the id of the mod that crashed EP147, or whose `init()` was running when EP147 stopped | one id | across reboots |

`/tmp` is in RAM, so a crash followed by a power-cycle loses the logs. Restarting EP147
(after a crash or with **Reload**) keeps them.

## Turning the mod's own lines on

`log()` lines are written only while `/home/root/settings/azmod-logs.on` exists. The core
checks for the file once a second, so turning it on or off needs no restart:

```sh
touch /home/root/settings/azmod-logs.on
tail -f /tmp/azmod-community/beat-counter.log
rm /home/root/settings/azmod-logs.on
```

The same file also turns on the official mods' diagnostics at the next EP147 start. Those add
file writes inside EP147 ([what it costs](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/LOGS.md#with-diagnostics-on)),
so decks in normal use run without the file.

## How a line is written

| | |
|---|---|
| From `init`, `tick`, page and surface callbacks, `post_ui`, audio taps and the mod's own threads | written at once on the calling thread: one open, write and close per line |
| From `on_frame` and the frame and touch filters | queued (8 lines, about 160 characters each) and written at the core's next tick; a full queue drops lines and the log says how many |
| Line length | cut at about 400 characters |
| Past 256 KB | lines are dropped until the next start; at the first line of a start, a file over 128 KB moves to `<id>.log.1` |

The harness counts log lines without writing them. Its `log-quiet` check fails a mod that logs
more than 30 lines in a 1000-tick soak ([The test harness](04-testing.md#the-test-harness)).

## A launched program's log

`log_path` names a plain file inside the mod's `data_dir`, which is on the USB, so the log
survives a power-off and can be read on a computer:

```c
static int start_helper_logged(void)
{
    const char *assets = C->assets_dir(MOD_ID), *data = C->data_dir(MOD_ID);
    if (!assets || !data) return -1;
    char path[512], log_path[512];
    snprintf(path, sizeof path, "%s/helper", assets);
    snprintf(log_path, sizeof log_path, "%s/helper.log", data);
    const char *const argv[] = { "--verbose", NULL };
    const az_launch_opts opts = { .size = sizeof opts, .nice = 10, .log_path = log_path };
    return C->launch(MOD_ID, path, argv, &opts);
}
```

| | |
|---|---|
| Refused | a path outside `data_dir`, a link, `.` or `..`, or a file with a second hard link |
| Writes | appended; a program that writes faster than the USB accepts waits on its pipe |
| Past 1 MB | the file moves to `<log>.1`, replacing the previous one, and a new one starts |
| `NULL` | output discarded |

`data_dir` returns `NULL` while that USB is out of the deck ([API reference](API.md#files-your-folders-on-the-usb-stick)),
so build the path again for each `launch`.

## Reading them

| Where | Command |
|---|---|
| A deck, with [SSH access](04-testing.md#ssh-access) | `ssh -i ~/.azmods/ssh/xdjaz_ecdsa root@DECK-IP 'tail -f /tmp/azmod-community/<id>.log'` |
| The [XDJ-AZ emulator](08-emulator.md) | from the emulator repository: `tools/az-ssh 'tail -f /tmp/azmod-community/<id>.log'` |
| A handoff program | `journalctl -u azmod-handoff --no-pager` over SSH or `tools/az-ssh` |
| A launched program | the file in `<stick>/MODS/community-mods/<id>/data/`, on the deck or with the USB in a computer |

To trace a crash block back to a function and line, see [Finding where it crashed](04-testing.md#finding-where-it-crashed).

## In a bug report for a mod

| Attach | Answers |
|---|---|
| `/tmp/azmod-community/loaded` and `<id>.log` (with diagnostics on, when the problem can be reproduced) | whether the mod loaded, what the core refused, what the mod logged |
| `/tmp/azmod-crash.log` | where EP147 crashed, and whether the core blamed the mod |
| the launched program's log, or `journalctl -u azmod-handoff` | what the program printed |
| the AZ-MODS version (Mod Menu > **System Info**) and the mod's `version` from its `azmod.json` | which builds were running |

Without SSH, Mod Menu > **System Info** > **Save Logs to USB** copies these files, with the rest
of the deck's logs and the AZ-MODS version, to `MODS/logs/<yyyymmdd-HHMMSS>/` on the USB
([what it copies](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/LOGS.md#save-logs-to-usb)).
It saves at any time, including while tracks play, and gives up after 30 seconds if nothing
answers. A launched program's log is already on the USB, in `MODS/community-mods/<id>/data/`;
the `azmod-handoff` journal is not in the copy.
