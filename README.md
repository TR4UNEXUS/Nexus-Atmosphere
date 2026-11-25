# Nexus Atmosphere

The complete firmware source for **Nexus Atmosphere 1.12.2 NX1**, including USB storage, game-content routing, the content database and the independent Nexus Loader. This is the source for the NX1 HATS packs, with Switch system version **23.5.0** support.

## What's changed

- All three HATS editions identify as NX1. Manager reads the edition and sequence from the pack identity. Firmware crash reports display Nexus-NX1.

- Preserve the successful same-drive sleep/resume path. After a failed wake, restart discovery only after FullAwake acknowledgement and quiescence, revoke old handles, and never replay uncertain writes.
- Rebuild invalid console routing archives from validated current maps in one checked save transaction. Retain rejected bytes and propagate actual I/O errors.
- Add optional backup diagnostics, including pending-repair, mount and semantic-map preflight failures; keep the original status/archive ABI.
- Provide a read-only live USB report snapshot with command submission, completion-wait and report timings. Retain fast failed commands in the bounded recorder too.
- Retain RF2 Album/hold-R overrides, final USB completion-report recovery, Nextendo-related DNS/mod paths, independent HOC and compressed game-data support.
- Include Manager 0.2.39 in the three HATS variants. Source is maintained separately.

Host regression checks and cross-builds passed. Physical console and Nextendo online testing remain necessary.

## Where to start

- [Build instructions](BUILDING.md)
- [Contributing](CONTRIBUTING.md)
- [USB integration source](https://github.com/TR4UNEXUS/Nexus-USB-Firmware)
- [Nexus Manager source](https://github.com/TR4UNEXUS/Nexus-Manager-Source) â€” Manager 0.2.39 is included in the packs; its source is maintained separately.
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
| `optional` | Historical combined HOC and HS2 experiments; not used by RF3 |
| `extras/nexus-sd` | Nintendo DNS blocking configuration |

USB support still spans several Atmosphere modules. Build the core and Nexus Loader from the same checkout. Boot through **Hekate â†’ Launch** with the separate `nexus-loader.kip` entry; launching `fusee.bin` directly does not load that external KIP.

The firmware cross-build, source/binary checks and host regression tests passed. RF3 console testing is still pending. These checks do not guarantee compatibility with every game, drive or system module. No generated firmware, private keys or console data are included here.

**Help building and improving Nexus is welcome.** USB compatibility, game management, performance, build tooling, documentation and testing are all useful areas to contribute. Open an issue with clear reproduction steps or a pull request describing the change and how it was tested.

## Support Nexus

If Nexus has been useful to you, consider [buying me a coffee](https://www.buymeacoffee.com/TechRepairs4u). Your support gives me more time to build new features and test fixes. Thanks for supporting my work!

## Credits and licence

Built on [AtmosphÃ¨re](https://github.com/Atmosphere-NX/Atmosphere) and the projects credited in its source. See [the upstream README](UPSTREAM-README.md), [LICENSE](LICENSE) and the notices beside third-party components. The overlay's UI libraries retain their own licences. Historical HOC source retains its notices; current HOC is distributed by [Horizon-OC](https://github.com/Horizon-OC/Horizon-OC). Nexus changes and build notes are maintained by TR4UNEXUS.
