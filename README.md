# SignalScope for Schwung

SignalScope is an unofficial interactive [Schwung](https://github.com/charlesvestal/schwung) tool for Ableton Move. It combines a compact system dashboard with an optional orchestra that turns nearby WiFi networks or active processes into pitched voices.

## Dashboard

- CPU use, CPU temperature, load, and top processes
- memory, storage, and uptime
- WiFi link and network counters
- nearby WiFi networks

The CPU temperature reader is intentionally separated from the module. A tiny root-owned SysV service calls `vcgencmd` every five seconds and publishes only the result to `/run/signalscope-cpu-temp`; the Schwung module itself remains unprivileged. The service executes its own root-owned code, not files in the writable Schwung module directory.

## Controls

- Bottom pads: select `CPU`, `MEM`, `PROC`, `WIFI`, `NET`, `DISK`, `UP`, or `ORCH`
- Main encoder: change tab
- `Play`: refresh the current tab; WiFi and orchestra views also rescan
- `Rec`: toggle automatic refresh
- `Back`: exit
- CPU: Steps 5 and 6 speak and reset peaks sampled since opening the tool. Turn on automatic refresh to track peaks between manual refreshes.
- DISK: Steps 5 and 6 speak the read and write rates on the user-storage partition. Disk samples only while the DISK view is open. Storage traffic can include delayed writeback and unrelated activity; a write is not proof that a set was saved.
- Orchestra: Steps 1 and 2 choose WiFi or process data, Step 3 refreshes, and Step 4 toggles sound
- Orchestra: Step 5 freezes the current voice lineup and strengths or returns to live data. Musical controls still work while frozen; switching the source unfreezes it.
- `Master`: orchestra gain
- Knobs 1-4: waveform, key, repeat rate, and decay
- Knobs 5-8: pan for voices 1-4
- Track buttons 1-4: Major 7, Minor 7, Suspended 7, or Pentatonic scale

## Install from Schwung Manager

Download `signalscope-module.tar.gz` from the [latest release](https://github.com/OnjLouis/schwung-signalscope/releases/latest), then open `http://move.local:7700`, choose the custom-module installer, and upload the tarball. Alternatively, give the custom installer this repository URL and Schwung Manager will follow `release.json` to the current package:

```text
https://github.com/OnjLouis/schwung-signalscope
```

The dashboard and orchestra work immediately after import. CPU temperature is optional because Move restricts `vcgencmd` to root. To enable it on a fresh installation, use the matching `scripts/signalscope-temperature.init` from a trusted source checkout. Run the following from that checkout, not from a copy on the Move's shared drive:

```bash
ssh root@move.local 'set -e; test ! -e /etc/init.d/signalscope-temperature; umask 022; cat > /etc/init.d/signalscope-temperature; chmod 0755 /etc/init.d/signalscope-temperature; update-rc.d signalscope-temperature defaults; /etc/init.d/signalscope-temperature start' < scripts/signalscope-temperature.init
```

Repeat this after a firmware update if the update removes the service from the system partition. The module remains installed and temperature works again without restarting Move. To replace an existing service rather than install it for the first time, use the local build installer below. Without the helper, SignalScope displays and announces temperature as unavailable rather than failing.

## Build from Source

The build script cross-compiles for Move's AArch64 Linux environment. Output is kept outside the repository.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -OutputRoot C:\Temp\signalscope-build
```

Set `ZIG_BIN` if Zig is not on `PATH`.

## Install a Local Build

The module is installed at `/data/UserData/schwung/modules/tools/signalscope`. Installation also requires root SSH access once to register the temperature service.

```bash
export SIGNALSCOPE_BUILD_ROOT=/path/to/signalscope-build
./scripts/install.sh
```

Restart Schwung or reboot Move after replacing the module DSP.

## Audit Notes

- ConnMan does not expose signal values in its service list. The current nearby-network fallback preserves ConnMan's ordering and derives approximate levels for orchestra dynamics; those fallback values are not RF measurements.
- Process, disk, and WiFi inspection use short synchronous system commands. Schwung normally requests them from the tool UI, but host callback-thread guarantees are not documented by the included API header.
- The connected-WiFi state is reset before each read so a disconnect does not leave stale SSID or bitrate data on screen.

## License

MIT. SignalScope is an independent community project and is not affiliated with Ableton.
