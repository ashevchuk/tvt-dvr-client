Pre-release build of the AppImage, made by the release workflow from the tagged commit.

## Run

```sh
chmod +x tvt-dvr-client-@VERSION@-x86_64.AppImage
DVR_USER=... DVR_PASS=... ./tvt-dvr-client-@VERSION@-x86_64.AppImage <host> [port]
```

Needs x86-64 Linux with glibc 2.28 or newer, the X11 client libraries, OpenGL, and a sound
server or ALSA. Running the file directly needs FUSE 2; otherwise use
`--appimage-extract-and-run`. The client takes the host and the credentials from the command
line and the environment, so starting it from a menu entry does nothing useful yet.

SHA-256 of the AppImage: `@SHA256@`

## What is inside

- the client, using the native protocol implementation (no manufacturer software);
- FFmpeg shared libraries, built without GPL or non-free parts and with only the H.264 and
  IMA ADPCM decoders, the Matroska muxer and the scaler (LGPL-2.1-or-later);
- raylib, linked statically (zlib licence).

The FFmpeg source release and its signature are attached to this release, as the LGPL requires.
The licence texts and the FFmpeg configuration are in `usr/share/doc/tvt-dvr-client` inside the
AppImage (`--appimage-extract`).

## Status

Tested against one 16-channel DVR (firmware 3.3.0.P-3531-00). Other models and firmware are
untested. Not implemented: PTZ, device configuration, alarm events, reverse playback and
reconnecting after a network loss.

The DVR sends the user name and password in cleartext and does not encrypt the media. Do not
expose it to the internet.
