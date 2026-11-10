# Building Nexus Atmosphere

Use Linux or WSL with the devkitPro Switch and ARM toolchains, GNU Make, Git, Python 3 and zip. The source needs devkitA64, devkitARM, libnx, deko3d and the Switch build tools, including uam. Install these through devkitPro; do not copy a toolchain from another application project.

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

The main tree preserves the current development Loader. `optional/loader-no-hoc` and `optional/loader-hoc` preserve the HS2 alternatives. In a separate clean checkout, copy the desired alternative's contents over `stratosphere/loader` before building. The separated HOC Loader also needs `optional/hoc/common` in its include path and the matching helper. Read [the optional HOC notes](optional/hoc/README-NEXUS.md) before using it. These optional sources do not imply that arbitrary HOC releases are compatible.

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
