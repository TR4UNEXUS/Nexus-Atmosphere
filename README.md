# Nexus Atmosphere

The complete firmware source for **Nexus Atmosphere 1.12.2 HS3R1**, including USB storage, game-content routing, the content database and the independent Nexus Loader. This is the source for the HS3R1 HATS test packs, with Switch system version **23.5.0** support.

## What's changed

USB game-loading hooks now live in a separate Nexus Loader. The original Loader remains available for stock operation or unmodified upstream HOC. The HOC test pack uses **HOC 2.6.1**; USB code is no longer merged into its KIP. HS3R1 fixes the shared filesystem-session conflict found in the first HS3 boot test.

The source also includes **Nexus USB Content 1.3.0**: a blue/purple drive panel, free/total space, usage bar, install route, safe eject and reconnect. It uses the existing firmware's checked eject protocol and keeps drive information visible while waiting for eject status.

Earlier N4R2 work remains included: compressed game-data reads, USB-to-USB install fixes, paged USB maps, console-side mapping backups, sleep/wake recovery and cheat-service protections. SD and internal-storage installs keep their selected destination when USB is connected. See [the changelog](CHANGELOG-NEXUS.md).

## Where to start

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md)
- [USB integration source](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)
- [Nexus Manager source](https://github.com/TR4UNEXUS/Nexus-Manager-Source) — Manager 0.2.24 is included in the packs; its source is maintained separately.
- [Ready-to-use packs](https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases)

| Folder | Purpose |
| --- | --- |
| `libraries/libstratosphere/source/usb` | USB transport, storage and filesystem support |
| `stratosphere/ams_mitm/source/usb_mitm` | Shared USB service and content routing |
| `libraries/libstratosphere/source/ncm` | Registration, routing indexes and database storage |
| `stratosphere/nexus_loader` | Independent USB-aware Loader and filesystem proxy |
| `stratosphere/loader` | Original Loader, replaceable by compatible upstream HOC |
| `stratosphere/sm` | Service routing between the two Loaders |
| `companions/usb-content-overlay` | Nexus USB Content 1.3.0 source and tests |
| `tests/nexus-integration` | Host regression tests |
| `optional` | Historical combined HOC and HS2 experiments; not used by HS3R1 |
| `extras/nexus-sd` | Nintendo DNS blocking configuration |

USB support still spans several Atmosphere modules. Build the core and Nexus Loader from the same checkout. Boot this test through **Hekate → Launch** with the separate `nexus-loader.kip` entry; launching `fusee.bin` directly does not load that external KIP.

The firmware cross-build, source/binary checks and host regression tests passed. Full HS3R1 console testing is still pending. These checks do not guarantee compatibility with every game, drive or system module. No generated firmware, private keys or console data are included here.

**Help building and improving Nexus is welcome.** USB compatibility, game management, performance, build tooling, documentation and testing are all useful areas to contribute. Open an issue with clear reproduction steps or a pull request describing the change and how it was tested.

## Credits and licence

Built on [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) and the projects credited in its source. See [the upstream README](UPSTREAM-README.md), [LICENSE](LICENSE) and the notices beside third-party components. The overlay's UI libraries retain their own licences. Historical HOC source retains its notices; current HOC is distributed by [Horizon-OC](https://github.com/Horizon-OC/Horizon-OC). Nexus changes and build notes are maintained by TR4UNEXUS.
