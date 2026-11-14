# Nintendo DNS blocking

Copy the `atmosphere` folder here to the SD card root. The three hosts files cover the fallback, CFW sysMMC and CFW emuMMC paths. They come from the [NH Switch Guide's maintained block list](https://nintendohomebrew.com/assets/emummc.txt).

Enable DNS MITM in `atmosphere/config/system_settings.ini`, then restart fully into CFW. Nintendo services are blocked; other internet services remain allowed. The two connection-test domains point to the guide's test server.

Check `atmosphere/logs/dns_mitm_startup.log` or run 90DNS Tester after boot. A custom `emummc_XXXX.txt` file takes priority, so it must contain the same rules if one is present.
