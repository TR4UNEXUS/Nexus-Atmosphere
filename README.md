# Nexus Atmosphere

The complete firmware source for **Nexus Atmosphere 1.12.2 N1**, including USB storage, game-content routing, content database changes and Loader integration.

This source snapshot includes the U2 SD installation fix, explicit USB installation commands, the SL7 sleep/wake recovery work and the paged content database. SD and internal-storage installations keep their selected destination when a USB drive is connected.

The updated base includes support code for system version 23.5.0, the Nexus splash screens, the USB-only Loader and the matching HOC 2.6.0 Loader source. The Nintendo blocking files used by the packs are included under `extras/nexus-sd`. See [what changed](CHANGELOG-NEXUS.md).

## Where to start

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md) — help with development, testing and documentation is welcome.
- [USB integration source](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)
- [Nexus Manager source](https://github.com/TR4UNEXUS/Nexus-Manager-Source)
- [Ready-to-use packs](https://github.com/TR4UNEXUS/Nexus-USB-HATS/releases)

## Source layout

| Folder | Purpose |
| --- | --- |
| `libraries/libstratosphere/source/usb` | USB transport, mass storage and filesystem support |
| `stratosphere/ams_mitm/source/usb_mitm` | Shared USB service and filesystem routing |
| `libraries/libstratosphere/source/ncm` | Content registration, routing and database storage |
| `stratosphere/loader` | Loader integration |
| `tests/nexus-integration` | Host tests for routing, registration, database and wake behaviour |
| `optional/loader-hoc-2.6.0` | The combined HOC Loader used by the current HOC pack |
| `optional/hoc` | Earlier HS2 separation experiment, helper and controls |
| `extras/nexus-sd` | Nintendo DNS blocking files for both CFW boot modes |

USB support spans several firmware modules. Build the core and the selected Loader from the same checkout. The current HOC pack uses the combined HOC 2.6.0 Loader; the earlier HS2 separation experiment remains available for development.

This is a source release. Generated firmware, console data and private keys are not included. Hardware compatibility still needs testing across different drives and games.

**Help building and improving Nexus is welcome.** Contributions to the USB driver, game management, build process, documentation and testing are all useful. Open an issue with clear reproduction steps, or send a pull request with the change and how you tested it.

## Credits and licence

Built on [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) and the projects credited in its source. See [the upstream README](UPSTREAM-README.md), [LICENSE](LICENSE) and the notices beside third-party components. Optional HOC components retain their own notices. Nexus changes and build notes are maintained by TR4UNEXUS.
