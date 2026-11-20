# Nexus Atmosphere changes

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
