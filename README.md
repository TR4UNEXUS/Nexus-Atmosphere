# Nexus Atmosphere

The complete Atmosphere source tree used by Nexus USB. It brings together the USB storage driver, game-content routing, content database changes and Loader integration.

This source snapshot includes the U2 SD installation fix, explicit USB installation commands, the SL7 sleep/wake recovery work and the paged content database. SD and internal-storage installations keep their selected destination when a USB drive is connected.

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
| `optional` | HOC-free and separated HOC Loader sources, helper and controls |

USB support spans several firmware modules. It cannot be updated safely by replacing an unrelated standalone KIP. The optional separated HOC helper needs its matching Loader and controls; a standard combined HOC KIP is not an interchangeable replacement.

This is a source release. Generated firmware, console data and private keys are not included. Hardware compatibility still needs testing across different drives and games.

## Credits and licence

Built on [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere) and the projects credited in its source. See [the upstream README](UPSTREAM-README.md), [LICENSE](LICENSE) and the notices beside third-party components. Optional HOC components retain their own notices. Nexus changes and build notes are maintained by TR4UNEXUS.
