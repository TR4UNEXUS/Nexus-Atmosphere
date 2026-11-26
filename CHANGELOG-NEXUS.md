# Nexus Atmosphere changes

## NX1 safe eject and USB Content 1.3.3

- Close a USB game on HOME, then use **Safely eject USB** in Nexus USB Content. Loader releases the temporary base archive used for title lookups and failed launches. Running games, open files and transfers still block removal.
- The overlay can ask the firmware to check what is using the drive. A purple check and chime mark a completed eject. A teal plus shows a connection and an amber minus shows an unexpected disconnect.
- Standard No-HOC, Standard HOC and Nexus-Themed HOC use the same USB service, independent Loader, overlay and notification artwork. The themed edition draws its purple splash from the JPEG embedded in its boot module.

## 1.12.2 NX1

All three editions share the NX1 version. The firmware and crash reports use this identity. The existing USB sleep/wake recovery and independent loader are included. The packs include Switch Manager 0.2.39 and the colour USB Content overlay.

## 1.12.2 RF3

- Preserve the successful same-drive sleep/resume path. After a failed wake, restart discovery only after FullAwake acknowledgement and quiescence, revoke old handles, and never replay uncertain writes.
- Rebuild invalid console routing archives from validated current maps in one checked save transaction. Retain rejected bytes and propagate actual I/O errors.
- Add optional backup diagnostics, including pending-repair, mount and semantic-map preflight failures; keep the original status/archive ABI.
- Provide a read-only live USB report snapshot with command submission, completion-wait and report timings. Retain fast failed commands in the bounded recorder too.
- Retain RF2 Album/hold-R overrides, final USB completion-report recovery, Nextendo-related DNS/mod paths, independent HOC and compressed game-data support.
- Include Manager 0.2.31 in the three HATS variants. Source is maintained separately.

Host regression checks and cross-builds passed. Physical console and Nextendo online testing remain necessary.

## 1.12.2 HS3R1 — 21 November 2026 (test)

- Moved Nexus USB game-loading hooks to a separate Loader. The stock Loader remains replaceable by unmodified upstream HOC; both packs use the same Nexus core and USB Loader.
- Fixed the HS3 boot conflict where two Loaders competed for a single filesystem service session. Nexus owns the native session and serves backend filesystem requests on a dedicated thread.
- Preserved service access checks, external-code handling, opaque Loader pins and debugger/module queries used by tools such as SaltyNX. Hardware compatibility still needs testing.
- Added Nexus USB Content 1.3.0 source, its UI dependency and host tests. The revised menu shows drive details and free/used/total space, with safe eject and reconnect. Drive information no longer depends on the eject heartbeat; bottom diagnostics are removed.
- Retained the N4R2 compressed-read, USB install, mapping-backup and wake-recovery fixes.
- HOC test pack references unchanged upstream HOC 2.6.1. No-HOC omits HOC. Both require the supplied Hekate Launch setup and include Manager 0.2.24.

Firmware builds and host checks passed. These remain test packs pending full on-console validation.

## 1.12.2 N4R2 — 20 November 2026

- Fixed compressed game-data reads crossing a full buffer. The shared reader no longer advances past a block, addressing the Silksong launch failure and the same path in other games.
- Fixed pending USB mapping writes during content-storage shutdown and backup failure unwinding, addressing the USB-to-USB install abort.
- Expanded mapping capacity to 4,096 title rows and 262,144 content entries with paged storage. These are bounded limits, not unlimited storage.
- Added automatic checked routing-index backups in a dedicated console system save, including existing valid maps at startup. Game data, tickets and saves are not included. Manager can preview restoration of missing links.
- Added a bounded small-read cache with invalidation on writes, disconnects and wake, and improved read access while writes are queued. Hardware speed gains have not been measured.
- Improved XCI inspection for trimmed files and optional leading key areas. This does not mount an XCI as a cartridge.
- Included the N2 boot-module isolation and N3 cheat-service error handling changes, and the current matching HOC 2.6.0 Loader source.
- Standard packs use the original Atmosphere splash and official Hekate appearance while retaining the Nexus build version.

The no-HOC N4R2 firmware and Manager 0.2.24 passed the reported Switch test, including Silksong gameplay. The HOC variant was rebuilt and binary-checked but still needs its own console test. Host regression tests cover the repaired compressed-read path; they do not establish that every game, cheat or system module works.

## 1.12.2 N1 — 14 November 2026

- Updated the firmware base to 1.12.2, including system-version 23.5.0 support code.
- Kept the USB installation, game routing, database and sleep recovery changes.
- Included the current USB-only Loader and matching HOC 2.6.0 Loader source.
- Preserved the Nexus splash screens and added the compressed splash decoder used by the build.
- Added Nintendo DNS blocking files for CFW sysMMC and emuMMC.
- Updated the build instructions and documented the separate components needed for a complete pack.

This update publishes source only. It does not publish new binary packs. The latest pack corrections restore sys-patch and the coloured overlay notifications; successful NSP launches still need confirmation on the console.
