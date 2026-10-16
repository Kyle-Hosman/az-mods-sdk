---
hide:
  - navigation
  - toc
---

<div class="az-hero" markdown>

# AZ-MODS Docs

Build, test and publish mods for the XDJ-AZ.

</div>

<div class="az-cards">
<a class="az-card" href="01-getting-started/"><strong>Getting started</strong><span>What a mod is, how it runs, what you need</span></a>
<a class="az-card" href="02-setup/"><strong>Set up your environment</strong><span>macOS, Windows and Linux, with one build command</span></a>
<a class="az-card" href="03-first-mod/"><strong>Your first mod</strong><span>The template step by step: init, tick and a settings page</span></a>
<a class="az-card" href="04-testing/"><strong>Testing on your deck</strong><span>The test harness, SSH, installing, logs and crash recovery</span></a>
<a class="az-card" href="05-hooks-and-firmware/"><strong>Hooks and firmware tags</strong><span>Changing EP147's own code on a specific firmware build</span></a>
<a class="az-card" href="06-publishing/"><strong>Packaging and publishing</strong><span>GitHub releases that users add by repository, and the optional Mod Index</span></a>
<a class="az-card" href="GUIDELINES/"><strong>Guidelines</strong><span>The crash-safety rules, good habits and a checklist before publishing</span></a>
<a class="az-card" href="API/"><strong>API reference</strong><span>Every service with its thread and its limits</span></a>
</div>

The [Guidelines](GUIDELINES.md) cover what keeps a mod stable.

## Repositories

| Repository | What it is |
|---|---|
| [xdj-az-mods](https://github.com/Kyle-Hosman/xdj-az-mods) | AZ-MODS itself: releases, issues and the source, including the core that loads mods |
| [xdj-az-emu](https://github.com/Kyle-Hosman/xdj-az-emu) | The XDJ-AZ emulator: the deck's program on your computer, for building and testing mods |
| [az-mods-sdk](https://github.com/Kyle-Hosman/az-mods-sdk) | The SDK: the `azmod-mod.h` header, the build container, the template mod, the `azmod-pack` tool, and this guide's source |
| [az-mods-examples](https://github.com/Kyle-Hosman/az-mods-examples) | Example mods covering the services, with their test scenarios |
| [az-mods-index](https://github.com/Kyle-Hosman/az-mods-index) | The Mod Index: repositories every deck shows by default, an optional route for publishing |

The SDK and every mod are GPL-3.0, and mods are published with their source (see
[License](01-getting-started.md#license)).
