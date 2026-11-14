# Building Nexus Atmosphere

Use Linux or WSL with the devkitPro Switch and ARM toolchains, GNU Make, Git, Python 3 and zip. The source needs devkitA64, devkitARM, libnx, deko3d and the Switch build tools, including uam. Install these through devkitPro; do not copy a toolchain from another application project.

The 1.12.2 N1 firmware was compiled with GCC 15.2.0 for both ARM toolchains. Use matching headers and libraries from the same SDK installation. After cloning, `sh scripts/build-nexus.sh` builds the core with the USB-only Loader. Set `LIBNX` and `PORTLIBS` if using a separate SDK prefix, and `JOBS` to change the parallel build count.

Clone this repository with Git so the version macros have a commit ID:

```sh
git clone https://github.com/TR4UNEXUS/Nexus-Atmosphere.git
cd Nexus-Atmosphere
export DEVKITPRO=/opt/devkitpro
export DEVKITA64="$DEVKITPRO/devkitA64"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITARM/bin:$PATH"
make -j2 nx_release
```

The normal target builds `package3`, Fusee and the companion modules from source. Outputs are under the individual component `out` directories; `fusee/out/nintendo_nx_arm_armv4t/release/package3` is the assembled core. `make -j2 dist-no-debug-nx_release` creates the upstream-style distribution after the build. This does not recreate a HATS pack or supply Hekate and other separately distributed applications.

The current code was developed with project-specific build caches. This public layout restores the source resources and removes those cache dependencies. A successful host test alone is not a complete clean cross-build or an on-console test; report compiler/version details with build failures. Do not mix objects or libraries from different source revisions.

## Loader variants

The main tree contains the USB-only Loader. After building the core, run `sh scripts/build-nexus.sh hoc` to build the current combined HOC 2.6.0 Loader from `optional/loader-hoc-2.6.0`. Its output is `optional/loader-hoc-2.6.0/out/nintendo_nx_arm64_armv8a/release/loader.kip`. The source carries generic HOC defaults; the release pack's existing overclock settings were retained separately when the pack was assembled.

`optional/loader-no-hoc`, `optional/loader-hoc` and `optional/hoc` preserve the older HS2 separation experiment. Read [the optional HOC notes](optional/hoc/README-NEXUS.md) before working on that experiment. These are historical alternatives, not the Loader selection for this release.

## Files needed when assembling a pack

Copy `extras/nexus-sd/atmosphere` to the SD card root for Nintendo DNS blocking. Enable `enable_dns_mitm = u8!0x1` in the `[atmosphere]` section of `atmosphere/config/system_settings.ini`, then restart fully into CFW. An existing `emummc_XXXX.txt` hosts file takes priority over the generic emuMMC file.

The complete packs also include [sys-patch 1.6.3.0](https://github.com/impeeza/sys-patch/releases/tag/v1.6.3.0), [Ultrahand](https://github.com/ppkantorski/Ultrahand-Overlay), the Nexus USB Content overlay and the user's theme, notification settings and sounds. Those separate applications are not built by this repository. Include their boot flags, loader files and configuration when assembling a pack.

## Host tests

Python 3 and a host C++20 compiler are enough for the main routing checks:

```sh
python3 tests/nexus-integration/run_u2_registration_tests.py
python3 tests/nexus-integration/run_u1_metadata_routing_tests.py
python3 tests/nexus-integration/run_u1_ownership_tests.py
python3 tests/nexus-integration/run_db2_paged_tests.py
python3 tests/nexus-integration/run_usb_sleep_tests.py --out build/host-tests/sleep
```

These execute production logic with instrumented host backends. They do not emulate Horizon IPC, real USB transfers or physical power loss. Historical negative-control tools may require the older source revision passed explicitly.
