# tvt-dvr-client

An unofficial, native Linux client for network DVRs from the TVT family (the ones that ship the
"EDVR" web client and a Windows CMS). It talks to the device through the manufacturer's network
SDK and renders with [raylib](https://www.raylib.com/), decoding with libavcodec.

This project is not affiliated with, endorsed by, or supported by the DVR manufacturer. Product
and company names belong to their owners and are used only to describe compatibility.

## Features

- **Live view** in 1, 4, 9 or 16 tiles; the 1- and 4-up layouts use the main stream, denser grids
  use the sub stream. Audio of the selected camera.
- **Playback** in 1 or 2x2 tiles: calendar of recorded days, a zoomable 24-hour timeline per
  channel, pause, 1x/2x/4x speed, click-to-seek. Tiles are kept in sync by a master clock.
- **Download** of a selected period as a standard AVI (H.264 + PCM) into `~/Videos/dvr`.
- Two small command-line tools for experiments: `probe` (record a live stream) and
  `playback_probe` (list recordings and capture a playback).
- `tools/live_poc.py`: a live-video client in plain Python that talks to the device without the
  vendor SDK, as a first step towards dropping the SDK. The protocol notes are in
  [docs/protocol.md](docs/protocol.md); `tools/tap.py` is the capture proxy they came from.

## Status

Developed and tested against one 16-channel DVR (firmware `3.3.0.P-3531-00`) using SDK
`1.2.1.019` (2019-09-02) on Linux/X11. Other models and firmware may behave differently.
See [docs/device-notes.md](docs/device-notes.md) for what is known about the device behaviour.

Not implemented: PTZ, device configuration, alarm events, reverse playback, automatic reconnect
after a network loss.

## Security

The DVR sends the user name and password in cleartext and does not encrypt the media. Do not
expose its port to the internet; use a VPN or a separate VLAN.

## Requirements

- g++ with C++17, `make`, `pkg-config`
- development packages for raylib, libavcodec, libswscale, libavutil
- **the manufacturer's SDK**, which is not included here (see [vendor/README.md](vendor/README.md))

## Build

```sh
make                    # client and tools into bin/
make SDK_DIR=/path/to/sdk
```

## Run

Credentials are read from the environment so they never appear in the process list or shell history:

```sh
export DVR_USER=... DVR_PASS=...
./bin/tvt-dvr-client <host> [port]     # port defaults to 6036
```

| Where | Action |
|---|---|
| Camera list | assign the camera to the selected tile (`[a]` marks cameras with audio) |
| Tile | click to select it; audio follows the selected tile |
| Playback calendar | click a day with recordings |
| Timeline | left click seeks; wheel zooms; middle drag pans; right drag selects a period |
| Download button | saves the selected period of the selected camera |
| `Esc` | quit |

## Layout

```
src/client/   the GUI client (stream decoding, playback logic, UI)
src/tools/    command-line probes
docs/         notes on how the device and its SDK behave
vendor/       where you put the SDK (ignored by git)
```

## License

MIT, see [LICENSE](LICENSE). This applies to the code in this repository only; the SDK, raylib
and FFmpeg are covered by their own licenses.
