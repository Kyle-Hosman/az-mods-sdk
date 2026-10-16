# Security policy

## Reporting a problem

Report security problems **privately**, not in a public issue or on Discord:
use GitHub's private reporting for this repository
([Report a vulnerability](https://github.com/Kyle-Hosman/az-mods-sdk/security/advisories/new),
under the Security tab).

Include what is affected (version, firmware, platform), how to reproduce it, and what
an attacker could do with it. The maintainer aims to answer within a week, and asks for a
reasonable time to ship a fix before details are published. Reporters are credited in the
release notes unless they ask not to be.

## Scope

| Problem in | Report to |
|---|---|
| the SDK: `include/azmod-mod.h`, the build container, `azmod-pack` (in particular the checks it runs before a package can be installed), the test harness, the template, the guide | this repository |
| AZ-MODS itself: the core that loads mods, the deck scripts, the desktop app | [xdj-az-mods](https://github.com/Kyle-Hosman/xdj-az-mods/security/advisories/new) |
| a mod built with the SDK | that mod's repository |

## Supported versions

The latest SDK release, for the newest AZ-MODS core.
