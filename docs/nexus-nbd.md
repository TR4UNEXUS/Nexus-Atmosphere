# Network library (NBD) for Nexus

Optional mode that lets Nexus use a disk image on a home server, over the
network, instead of a physical USB drive. The console sees the image exactly
like a USB drive: the same exFAT/FAT32 mount, title routing, sleep/wake and
safe-eject code paths are used.

It is **off unless** `sdmc:/atmosphere/config/nexus-nbd.ini` exists. Without
that file nothing changes: no sockets are opened and no memory is allocated.

## How it works

- `usb::mass_storage::Device` gains a third transport, `Nbd`, next to BOT and
  UAS. It emulates the SCSI commands Nexus already issues (TEST UNIT READY,
  INQUIRY, READ CAPACITY 10/16, READ/WRITE 10/16, REQUEST SENSE,
  SYNCHRONIZE CACHE) as NBD requests over one TCP connection (`bsd:s`).
- The storage worker has an NBD loop (`Service::NbdWorkerThread`) that
  replaces usb:hs discovery and then runs the normal partition and volume
  mount.
- A dropped connection is reopened and the request replayed (reads and writes
  are idempotent) for up to `reconnect_ms`. After that the media is
  quarantined and remounted, as with an unplugged drive.
- Sleep/wake: identity is checked with the export size, the sector
  fingerprints and the sleep marker, after reconnecting.
- The bsd transfer memory (about 680 KiB) comes from the ams_mitm heap on
  first use only.

## Console configuration

`sdmc:/atmosphere/config/nexus-nbd.ini`:

```ini
host=192.168.1.10   ; IPv4 address of the NBD server
port=10809
export=nexus
reconnect_ms=20000  ; how long one I/O may wait for the server to come back
```

## Server

Any NBD server with the fixed-newstyle handshake works. Example with
nbd-server 3.x:

```ini
[generic]
    port = 10809
    allowlist = true

[nexus]
    exportname = /srv/nexus-library.img
    readonly = false
    flush = true
    fua = true
    authfile = /etc/nbd-server/allow   ; e.g. 192.168.1.0/24
```

The image can be a plain exFAT filesystem without a partition table (Nexus
accepts it as a super-floppy), for example a sparse file:

```sh
truncate -s 512G nexus-library.img
mkfs.exfat -L NEXUSNBD nexus-library.img
```

Notes from testing:

- Do not use `maxconnections = 1` with nbd-server 3.24: after the console is
  powered off without closing the connection the slot stays taken and every
  reconnect is refused until the server restarts.
- Lower the server's TCP keepalive (for example 30 s / 10 s / 3 probes) so a
  connection from a console that was forced off is dropped quickly.
- Never mount the image elsewhere while the console can connect.
- NBD has no authentication: keep it on the local network.

## Measured (Switch OLED, gigabit server, 1 MiB reads)

| Link | Sequential, 1 request | 4 requests in flight | 60 s average |
|---|---|---|---|
| Dock LAN | 19-25 MB/s | 36 MB/s | 32 MB/s, no stalls over 500 ms |
| Wi-Fi (2.4 GHz suspected) | 4-6 MB/s | 6 MB/s | 5.7 MB/s |

## Tested on a console

Important: the test console runs a third-party variant of Nexus (Nexus
Bacán from OhQueBacán), whose other modules differ from NX1. Any ams_mitm
built from NX1 source, **including an unmodified one**, freezes that system
when programs are opened. So this patch has not been tested on an official
NX1 installation yet. On the variant it did:

- boot and mount ("Nexus Network", 512 GB);
- install a title to "USB" through the Manager (written to the server) and
  launch it;
- reconnect after the server restarts.

Not yet tested: official NX1, long play sessions, sleep/wake with a running
title, pulling the network cable during play.
