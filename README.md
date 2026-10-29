# tvt-dvr-client

An unofficial, native Linux client for network DVRs from the TVT family (the ones that ship the
"EDVR" web client and a Windows CMS). It speaks the device's network protocol itself, so it needs
no proprietary library, renders with [raylib](https://www.raylib.com/) and decodes with FFmpeg.

This project is not affiliated with, endorsed by, or supported by the DVR manufacturer. Product
and company names belong to their owners and are used only to describe compatibility.

## Features

- **Live view** in 1, 4, 9 or 16 tiles; the 1- and 4-up layouts use the main stream, denser grids
  use the sub stream. Audio of the selected camera.
- **Playback** in 1 or 2x2 tiles (the device allows four playbacks at a time): calendar of
  recorded days, a zoomable 24-hour timeline per channel, pause, 1x/2x/4x speed, click-to-seek.
  Tiles are kept in sync by a master clock.
- **Download** of a selected period into `~/Videos/dvr`: a Matroska file with the H.264 video and
  PCM audio, timestamped from the device clock.

## Backends

The client reaches the device through one of two interchangeable backends:

| Backend | Needs | Notes |
|---|---|---|
| `native` (default) | nothing proprietary | the protocol is implemented in `src/proto`, see [docs/protocol.md](docs/protocol.md) |
| `sdk` | the manufacturer's SDK (`make SDK=1`) | the original path, kept for comparison; downloads are AVI files |

Both pass the same checks (`bin/devicecheck`). The native backend is also faster at downloads.

## Status

Developed and tested against one 16-channel DVR (firmware `3.3.0.P-3531-00`) on Linux/X11. Other
models and firmware may behave differently. See [docs/device-notes.md](docs/device-notes.md) for
what is known about the device behaviour.

Not implemented: PTZ, device configuration, alarm events, reverse playback, automatic reconnect
after a network loss.

## Security

The DVR sends the user name and password in cleartext and does not encrypt the media. Do not
expose its port to the internet; use a VPN or a separate VLAN.

## Requirements

- g++ with C++17, `make`, `pkg-config`
- development packages for raylib, libavformat, libavcodec, libswscale and libavutil
- optionally the manufacturer's SDK for the `sdk` backend; it is not included here (see
  [vendor/README.md](vendor/README.md))

## Build

```sh
make                    # the client and the checks into bin/
make test               # unit tests of the protocol layer and the audio decoder
make SDK=1              # also build the SDK backend and the SDK experiments
make SDK=1 SDK_DIR=/path/to/sdk
```

## Run

Credentials are read from the environment so they never appear in the process list or shell history:

```sh
export DVR_USER=... DVR_PASS=...
./bin/tvt-dvr-client <host> [port]                       # port defaults to 6036
./bin/tvt-dvr-client --backend sdk <host> [port]         # needs a build with SDK=1
```

| Where | Action |
|---|---|
| Camera list | assign the camera to the selected tile (`[a]` marks cameras with audio) |
| Tile | click to select it; audio follows the selected tile |
| Playback calendar | click a day with recordings |
| Timeline | left click seeks; wheel zooms; middle drag pans; right drag selects a period |
| Download button | saves the selected period of the selected camera |
| `Esc` | quit |

## Checking a device

```sh
./bin/devicecheck native <host> /tmp/test.mkv     # searches, live streams, playback, pause, download
./bin/protocheck <host>                           # the protocol layer alone
```

`bin/devicecheck` expects recordings from a particular day of the developer's device and will
need its time constants adjusted for yours.

## Layout

```
src/proto/    the wire protocol: framing, messages, and a logged-in connection
src/device/   the Device interface, the native and SDK backends, the file writer
src/media/    the audio decoder shared by the player and the file writer
src/client/   the GUI client (stream decoding, playback logic, UI)
src/tools/    command-line checks and SDK experiments
tools/        Python programs that use the protocol without the client, and the capture proxy
tests/        unit tests
docs/         the protocol and notes on how the device behaves
vendor/       where the SDK goes if you want the SDK backend (ignored by git)
```

## License

MIT, see [LICENSE](LICENSE). This applies to the code in this repository only; raylib, FFmpeg and
the manufacturer's SDK are covered by their own licenses.
