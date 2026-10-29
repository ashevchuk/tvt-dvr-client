# Device and SDK notes

Observations from a 16-channel DVR (firmware `3.3.0.P-3531-00`, 4 audio inputs) and SDK
`1.2.1.019`. They explain choices made in the client; none of this is vendor documentation.

## Connection

- The device listens on TCP 6036 (the web UI's `server.js` says `NetPort=6036`). The SDK's
  default of 9008 is not used by this device; pass the port to `NET_SDK_Login`.
- On connect the server speaks first: a 64-byte packet starting with the ASCII bytes `head`.
  The SDK recognises it.
- One login carries 16 simultaneous live streams.

## Live streams

- Each channel has two streams: main 928x576 and sub 352x288. The sub stream is the main
  stream's field of view squeezed horizontally; display it at 928:576.
- Video arrives as Annex-B H.264 with SPS/PPS in key frames. The first picture waits for a key
  frame, so up to a few seconds after start.
- libavcodec logs `SEI type 18 ... truncated` on nearly every frame; the payload is a vendor
  extension and the picture is unaffected.
- Audio arrives unprompted; `NET_SDK_OpenSound` only controls local playback through ALSA.
  Each audio frame is a 4-byte wrapper (`00 01 52 00`) followed by 840 bytes, which are five
  IMA ADPCM blocks of 168 bytes (8 kHz, mono). The format frame claims `nBlockAlign=1`; the
  168-byte block size was found from the data. The device sends audio in 200 ms bursts, so a
  ~300 ms jitter buffer is needed.
- Only the first four channels have audio inputs.

## SDK behaviour worth knowing

- The SDK writes diagnostics to stdout. Redirect file descriptor 1 to stderr before using
  stdout for a media stream.
- Returning from `main` without `NET_SDK_Logout`/`NET_SDK_Cleanup` while SDK threads are alive
  crashes the process.
- `NET_SDK_Cleanup` frees memory allocated with `new[]` using `delete`; AddressSanitizer reports
  it. It is inside the library and harmless in practice.
- The manual is a CHM file (`7z x` unpacks it; pages are GBK). Error codes are the positions
  in the `NET_SDK_SUCCESS` enumeration at the top of `DVR_NET_SDK.h`.
- `DD_TIME` holds broken-down local wall-clock time: `year` is years since 1900 and `month` is
  0-11; `nTotalseconds` can stay zero. Frame timestamps (`frameInfo.time`) are microseconds
  since the Unix epoch (UTC).

## Searching recordings

- `NET_SDK_FindNext*` returns `NET_SDK_TRY_LATER` while the search is still running. Retrying
  is required, otherwise the list is silently truncated. The end of a list is
  `NET_SDK_NOMOREFILE`.
- `NET_SDK_FindRecDate` lists days with recordings and may repeat a day.
- `NET_SDK_FindTime` returns recorded segments of one channel for a time range. It carries no
  record type.

## Playback

- `NET_SDK_PlayBackByTime` accepts exactly one channel; a longer list fails with
  `NET_SDK_PARAMETER_ERROR`. `NET_SDK_PlayBackByTimeSync` accepts several but never calls the
  data callback (it is meant for the SDK's own window), so it cannot be used here.
- At most four playback sessions can run at once per login; the fifth fails with
  `NET_SDK_DVR_NORESOURCE`.
- Frames are delivered through `NET_SDK_SetPlayDataCallBack` in real time, in the same format
  as live frames.
- A session started in a gap begins at that channel's next recording, which can be much later.
  A session started inside a segment begins at the preceding key frame, possibly in the previous
  segment, so frames earlier than the requested time must be decoded but not shown.
- One session running to the end of the day crosses gaps by itself.
- `NET_SDK_PlayBackControl` supports pause, resume, `FF` with `NET_SDK_RPB_SPEED_*`, normal
  speed and `SETPOS` (seconds since the Unix epoch). At 4x the audio is delivered at 4x as well.

## Downloading

- `NET_SDK_GetFileByTimeEx(..., bCustomFormat = FALSE)` writes a standard AVI with H.264 and PCM
  audio, about six times faster than real time. `TRUE` produces an opaque private format, and
  the callback mode also delivers the private format.
- The AVI header says 29.97 fps and contains filler slots; the real frame rate is 25 and the
  duration and audio/video sync are correct.
- Progress is `NET_SDK_GetDownloadPos` (0-100; 200 means a network error).
