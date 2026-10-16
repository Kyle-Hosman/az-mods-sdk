# AZ-MODS internals

These pages describe the AZ-MODS core itself: how it is built, how releases are signed, and
how a deck decides what to run. Writing a community mod does not need them; they are here for
anyone who wants to check the project's work, build the core, or run their own fork.

They live with the core's source in [xdj-az-mods](https://github.com/Kyle-Hosman/xdj-az-mods):

| Page | What it covers |
|---|---|
| [ARCHITECTURE.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/ARCHITECTURE.md) | how the core is put together: design principles, source layout, lifecycle, data flow and the audio path |
| [BUILDING.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/BUILDING.md) | building `azmod.so` and `azstem.so`, checking a build against an official release, and running your own build on your deck |
| [docs/UPDATES-AND-SIGNING.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/UPDATES-AND-SIGNING.md) | the mods channel, `manifest.json`, the signatures, the deck's trust pins and the developer override |
| [docs/COMMUNITY-MODS-INTERNALS.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/COMMUNITY-MODS-INTERNALS.md) | how the deck installs, approves and loads community mods |
| [docs/FIRMWARE-SUPPORT.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/FIRMWARE-SUPPORT.md) | which firmware builds the core supports and how it identifies them |
| [docs/USB-STICK.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/USB-STICK.md) | every file AZ-MODS keeps on the USB |
| [docs/LOGS.md](https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/docs/LOGS.md) | every log the deck writes |
