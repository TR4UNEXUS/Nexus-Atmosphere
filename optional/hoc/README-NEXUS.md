# Optional separated HOC sources

This directory preserves the HS2 helper (`Atmosphere/stratosphere/hoc_patch`), its matching Loader, shared protocol, status app, clock sysmodule and settings overlay. The `loader-no-hoc` sibling provides the USB Loader without HOC integration.

The split helper uses ABI 1 and must be used with its matching Loader and controls. Do not substitute an ordinary combined HOC KIP. The code contains generic source defaults, not a console's personal settings.

The tools here are the historical HS2 assembly and verification tools. They expect source/build inventories and dependency receipts from that workflow; they are not a one-command clean build of the public repository. Their external paths are configured by `NEXUS_HOC_ORIGINAL`, `NEXUS_HOC_CORE`, `DEVKITPRO` and `LIBNX`. Some legacy commands still target Windows devkitPro/MSYS2. No cached libraries or local receipts are shipped. Help turning this into a reproducible cross-platform build is welcome.

For the base firmware, use the root BUILDING.md. The optional module makefiles accept `NEXUS_HOC_CORE` pointing at the full core source tree and `NEXUS_PINNED_ARCHIVE` pointing at a matching freshly built libstratosphere archive. Add this directory's `common` folder to the compiler include path. Build the helper and Loader against the same headers and archive.

The clock sysmodule and overlay have their own makefiles under `hoc-clk`; the status application's makefile is under `diagnostics`. All upstream licences remain with their source.
