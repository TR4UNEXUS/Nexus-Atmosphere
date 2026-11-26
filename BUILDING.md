# Building Nexus Atmosphere NX1

Use Linux or WSL, Git, Python 3, GNU Make and the devkitPro Switch/ARM toolchains: devkitA64, devkitARM, libnx, deko3d and Switch build tools including uam. The release build used GCC 15.2.0 for both ARM toolchains. Keep SDK headers and libraries from a matching installation; do not reuse objects from another revision.

```sh
git clone https://github.com/TR4UNEXUS/Nexus-Atmosphere.git
cd Nexus-Atmosphere
export DEVKITPRO=/opt/devkitpro
export DEVKITA64="$DEVKITPRO/devkitA64"
export DEVKITARM="$DEVKITPRO/devkitARM"
export LIBNX="$DEVKITPRO/libnx"
export PORTLIBS="$DEVKITPRO/portlibs/switch"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITARM/bin:$PATH"
JOBS=4 sh scripts/build-nexus.sh
```

The core target builds all required modules, including the separate Nexus Loader. To rebuild just that Loader, use `sh scripts/build-nexus.sh loader`. The wrapper supplies version macros for Git checkouts; output hashes will differ from the original workstation build as build paths, commit IDs and compiler inputs change.

Core and Loader outputs for all three variants:

- `fusee/out/nintendo_nx_arm_armv4t/release/package3` â†’ `atmosphere/package3`
- `stratosphere/nexus_loader/out/nintendo_nx_arm64_armv8a/release/nexus_loader.kip` â†’ `atmosphere/kips/nexus-loader.kip`

Verify the output filename from the build before packaging. A normal Atmosphere distribution target alone does not assemble a complete Nexus HATS pack.

## HOC and No-HOC

Both packs use the same core and Nexus Loader. HOC is no longer compiled into the Nexus Loader. The HOC pack adds the unchanged upstream [HOC 2.6.1 release](https://github.com/Horizon-OC/Horizon-OC/releases/tag/2.6.1), with its supporting files. The referenced `dist.zip` SHA-256 is `cd943d0c8f7c8454c1fa85b87a55f6ca3b7a7c78084f37183929bc76c13455a4`.

The three NX1 packs load these files through their CFW (emuMMC) and CFW (sysMMC) entries:

```ini
pkg3=atmosphere/package3
pkg3kip1skip=ams_mitm
kip1=atmosphere/kips/nexus-loader.kip
kip1=bootloader/nexus-eject/ams-mitm.kip
```

The replacement service comes from `stratosphere/ams_mitm/out/nintendo_nx_arm64_armv8a/release/ams_mitm.kip`. Both HOC editions keep their existing HOC and exosphere entries. The stock entry uses its original configuration.

For HOC, also include its supplied files and add:

```ini
kip1=atmosphere/kips/hoc.kip
secmon=atmosphere/exosphere.bin
```

Do not use an old combined Nexus/HOC Loader with HS3R1. `optional/loader-hoc-2.6.0`, `optional/loader-no-hoc`, `optional/loader-hoc` and `optional/hoc` are historical source, not current build selections. Updating HOC no longer requires merging USB hooks, but the HOC version must still support the installed system firmware. Preserve the Nexus core, separate Loader and boot entry. Direct Fusee boot does not load the external Loader in this setup.

The original Loader can own HOC's clock patches while Nexus handles USB game loading. HS3R1 owns the single native `fsp-ldr` session in Nexus and proxies the backend's filesystem requests on a dedicated thread. See `stratosphere/nexus_loader`, `stratosphere/sm` and `ldr_nexus_routing.hpp` for the integration.

## USB Content overlay

Run the following from the Atmosphere source root:

```sh
sh companions/usb-content-overlay/notifications/prepare-library.sh
make -C companions/usb-content-overlay -j4
python3 companions/usb-content-overlay/tests/run.py
python3 tests/nexus-integration/run_hs3_frontend_tests.py
python3 tests/nexus-integration/run_eject_owner_tests.py
python3 tests/nexus-integration/run_usb_notification_tests.py
```

From `companions/usb-content-overlay/out/`, copy both `switch/` and `config/` to the SD root. All three editions use the four `ams-usb*.rgba` badges under `config/ultrahand/assets/notifications/`. The HATS packs supply Ultrahand 2.5.4, its notification sound and the enabled notification defaults. The user's Ultrahand mute setting applies.

## Other pack components

The standard packs use official Hekate 6.5.5 / Nyx 1.9.5 appearance and the original Atmosphere splash. Hekate, Ultrahand, overlay loader, notification assets, sys-patch 1.6.3.0 and Nexus Manager are distributed separately from the core build. Include their licences, configuration and boot flags when assembling a complete pack. Manager source is in [Nexus-Manager-Source](https://github.com/TR4UNEXUS/Nexus-Manager-Source).

Copy `extras/nexus-sd/atmosphere` for the DNS hosts configuration and set `enable_dns_mitm = u8!0x1` under `[atmosphere]` in `atmosphere/config/system_settings.ini`. An existing `emummc_XXXX.txt` takes precedence over the generic emuMMC hosts file. Preserve users' USB maps and console data during upgrades.

## Host checks and limits

Run from this repository with Python 3, GCC/G++ and address/undefined-behaviour sanitizers:

```sh
python3 tests/nexus-integration/run_hs3_frontend_tests.py
python3 tests/nexus-integration/run_hs3_sm_tests.py
python3 tests/nexus-integration/run_compressed_read_tests.py
python3 tests/nexus-integration/run_u2_registration_tests.py
python3 tests/nexus-integration/run_u1_metadata_routing_tests.py
python3 tests/nexus-integration/run_u1_ownership_tests.py
python3 tests/nexus-integration/run_db2_paged_tests.py
python3 tests/nexus-integration/run_usb_sleep_tests.py --out build/host-tests/sleep
```

The new tests exercise production service methods with instrumented backends, including opaque pin ownership, error propagation, module queries, service aliases and a single filesystem session. They do not reproduce Horizon scheduling, physical USB removal, suspend/resume or every system-module combination. Safe eject after closing a game was tested on a Switch.

## Regression checks and themed appearance

The standard source retains the original Atmosphere splash. For the Nexus-themed appearance, copy the files under `variants/nexus-themed/` over the source root before building. The themed boot module decodes its embedded Nexus JPEG into the display framebuffer with TJpgDec. Keep Hekate’s CFW launch splash setting as supplied in the themed pack. HOC remains a separate external component.

```sh
python3 tests/nexus-integration/run_homebrew_override_tests.py
python3 tests/nexus-integration/run_usb_sleep_tests.py
python3 tests/nexus-integration/run_usb_map_backup_tests.py
g++ -std=c++20 -O2 -fsanitize=undefined -I libraries/libstratosphere/include tests/nexus-integration/usb_report_completion_tests.cpp -o /tmp/nexus-completion-test
/tmp/nexus-completion-test
```

The archive tests use OpenSSL's host SHA-256 implementation (`libssl-dev` on Debian/Ubuntu) and faulting in-memory filesystem boundaries. They do not write a console or an actual drive. The sleep tests exercise production logic with simulated USB/PSC services; physical recovery remains hardware-dependent. The optional backup diagnostic IPC command does not replace the existing backup/status protocol.

The published tree preserves upstream notices and public build wrappers. Branding resource renames are documented in the existing guide; the firmware binaries were built from the pinned release workspace, not claimed to be byte-identical to a build using this repository's own Git revision.

## NX1 identity

Run `python3 tools/nexus_pack_identity.py --header --check` from the repository root to verify that the compiled identity matches nexus-release.json. The standard and themed editions use this same header.
