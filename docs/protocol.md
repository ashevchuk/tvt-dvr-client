# Wire protocol (TCP 6036)

What the vendor SDK and the web client exchange with the DVR, reconstructed from LAN traffic
captured with `tools/tap.py` on one 16-channel device (firmware `3.3.0.P-3531-00`). Everything
here was observed on the wire; nothing was taken from vendor code. All integers are
little-endian. Items marked *(hypothesis)* fit the captures but have not been tested directly,
and anything not listed is unknown.

`tools/live_poc.py` is a complete working client for the parts below (login, live video).

## Security

The credentials travel **in cleartext**: the login request contains the user name and password
as plain ASCII, and the media is not encrypted. Anyone on the same network segment can read
them. Do not expose the port to the internet; use a VPN or a separate VLAN.

## Greeting

Right after the TCP connection is accepted the device sends 64 bytes:

```
68 65 61 64  00 00 00 00  12 27 00 00  04 00 00 00  03 00 00 00  00 ...
"head"                      0x2712       4            3            zero padding
```

The vendor client sends its login after receiving them; `tools/live_poc.py` does the same. The
meaning of the numbers is unknown.

## Framing

```
"1111" | u32 length | body[length]
```

- A frame with `length = 0` is a keepalive. The vendor client sends one every 7.5 seconds and
  nothing is sent back.
- A body is either a **message** or a **PACK fragment** (body starts with `PACK`).

### Message

```
u32 cmd | u32 a | u32 b | u32 payload_len | payload[payload_len]
```

- `payload_len` always equals `length - 16`.
- Client messages use `a = 0, b = 4`.
- Replies to requests carry `a = 0x002b1080` (constant on the tested device). Notifications and
  media carry `a = 0xffffffff`. `b` is `4` for replies and media and takes other values on some
  notifications. The meaning of `a` and `b` is unknown.

### PACK fragment

Large messages (video key frames, mostly) are split into fragments carried in separate frames:

```
"PACK" | u32 1 | u32 count | u32 total | u32 index | u32 chunk_len | u32 0 | chunk[chunk_len]
```

`index` runs from 1 to `count`; concatenating the chunks gives one message of `total` bytes
(its 16-byte header included). Chunks are 10240 bytes except the last. Frames that fit in one
frame are sent unfragmented.

## Requests used for live video

| cmd | payload | meaning |
|---|---|---|
| `0x0101` | 120 bytes, see below | log in |
| `0x0201` | 36 bytes, see below | start streaming |
| `0x0202` | 36 bytes, same layout | add channels to a connection that is already streaming |
| `0x0203` | 36 zero bytes | stop streaming (the vendor client sends this before closing) |
| `0x0501` | 44 bytes | search: recorded days, or recorded segments (see [Searching](#searching-recordings)) |
| `0x0502` | 44 bytes | start playback or a download |
| `0x0507` | 44 bytes | stop playback or a download |
| `0x050b` | 44 bytes | acknowledge received playback messages |

The vendor client also sends `0x0801` (no payload; the device answers with several information
messages) and `0x050c` (no payload). The device streams without either of them.

### Login (`0x0101`) payload

```
offset  size  field
0       4     u32 1
4       4     u32 0
8       36    user name, ASCII, zero padded
44      36    password, ASCII, zero padded
80      36    zeros
116     4     u32 4
```

The reply is `cmd = 0x00010001` with a 348-byte payload on success (it begins with what look
like per-function channel masks, `0xffffffff, 0x0000ffff, ...`) and `cmd = 0x00010002` with a
4-byte payload on failure. The failure value was not recorded. One failed attempt did not lock
the account.

### Start streaming (`0x0201`) payload

Nine words:

```
w0 = 0
w1 = main-stream channel mask      (bit N = channel N, counting from 0)
w2 = 0
w3 = sub-stream channel mask
w4, w5, w6 = 0
w7 = union of the two masks
w8 = 0
```

Verified for the main stream on channels 0 and 5 and for the sub stream on channels 0, 3 and 5.

**One connection carries several streams.** The vendor client opens a single TCP connection even
with four live tiles. The first channel is requested with `0x0201`; every further channel is added
with `0x0202`, whose masks are cumulative (`0x1`, then `0x3`, then `0x7`, then `0xf`), so each
request describes the whole set of active channels. Media messages are told apart by the channel
number in the frame header (`w6`). Checked with channels 0, 1, 3, 5 on the main stream and
0 to 3 on the sub stream. Removing a single channel from a running set was not captured; `0x0203`
with zeros stops everything. Requesting both streams of one channel at once was not tried.

## Media messages

Streaming starts with the replies to `0x0201` and then `cmd = 0x0a000001` messages. The payload
is a 60-byte frame header (fifteen words) followed by `w2` bytes of data.

| word | meaning |
|---|---|
| w0 | `1` on key frames and audio frames, `0` on other video frames |
| w1 | kind: `1` video, `2` audio, `3` video format, `4` audio format, `9` end of data (no data, only in playback and downloads) |
| w2 | length of the data after the header |
| w3, w4 | width and height (video and video format only) |
| w6 | channel number, counting from 0 |
| w5 | an address inside the device; not data |
| w11, w12 | low and high 32 bits of a timestamp in microseconds since the Unix epoch (UTC) |
| w13 | a second microsecond counter, advancing in step with w11 *(hypothesis: time since stream start)* |
| w14 | `0x108` in all captures |

Other words (w7-w10) were constant or not understood.

- **Video** (kind 1): Annex-B H.264, SPS/PPS inside key frames; consecutive frames are 40 ms
  apart at 25 fps. The same frames the SDK hands to its callback.
- **Audio** (kind 2): 840 bytes, the same 4-byte wrapper and IMA ADPCM blocks described in
  [device-notes.md](device-notes.md); 5 per second.
- **Formats** (kinds 3 and 4): 40-byte video and 18-byte `WAVEFORMATEX`-style audio descriptions
  sent at the start.

## Notifications

Messages with `cmd = 0x09000001` to `0x09000008` and `0x00080001` (the latter lists the
device's disks) arrive around the start of a session; `0x09000002`/`0x09000004` also turn up
occasionally during streaming. They were not needed for live video and are not decoded.

## Searching recordings

Both searches are `0x0501` with 44 bytes (eleven words) and are answered by `cmd = 0x00050001`.

- **Recorded days**: all eleven words zero. The reply has a 16-byte header, then one 8-byte
  entry per day: a date packed as `year << 16 | month << 8 | day` (`0x07ea0a10` is 2026-10-16)
  and the Unix time of that day's start. The device may list a day more than once (it did for
  the two days around the clock change).
- **Recorded segments** of one channel:

  ```
  w0 = 1, w1 = 0
  w2 = channel mask, w3 = 0, w4 = channel mask, w5 = 0
  w6 = range start (Unix time), w7 = range end
  w8 = 0, w9 = 0xffffffff (all record types), w10 = 0
  ```

  The reply has a 16-byte header, then one 12-byte entry per segment: `type` (always 0 in the
  captures), `start`, `end`, as Unix times. 265 entries came back in one 3196-byte message,
  matching what the SDK reported.

The header words of both replies were not decoded.

## Playback

A playback is requested with `0x0502`:

```
w0 = 0xffffffff
w1 = session id (the vendor client uses 0x0a)
w2 = channel mask, w3 = 0, w4 = channel mask, w5 = 0
w6 = start (Unix time), w7 = end
w8 = 0
w9 = 0xf          (probably the record types to play: manual, schedule, motion, sensor)
w10 = 0
```

The device answers with `cmd = 0x00050003` carrying `0x3e8`, then media messages as for live
video. Playback is one channel per request. The first frame is the key frame at or before the
requested start, so it can be a few seconds early or even in an earlier segment. A session
started inside a gap begins at that channel's next recording. When the range ends the device
sends a media message of kind 9.

### Flow control and speed

The device sends at most about 24 media messages (video and audio) that have not been
acknowledged and then waits. An acknowledgement is `0x050b` with the session id in `w1` and a
sequence number in `w8`; the number counts media messages from 1 within the session, and an
acknowledgement covers every message up to it (acknowledging each message and acknowledging
every eighth both worked).

**Playback speed is therefore set by the client**, by how fast it acknowledges:

| acknowledgements | result |
|---|---|
| none | the device stops after ~24 messages |
| paced by the frame timestamps | real time (measured 1.00x) |
| paced at N times that rate | N times real time (the vendor client's fast forward does exactly this; it sends no speed command) |
| immediately | as fast as the device delivers, about 6.5x to 7x |

A slow motion should work the same way but was not tried.

### Pause and seek

- **Pause**: stop acknowledging. No frames were lost across the pause, and the session survived
  a 45-second pause with only keepalives. The vendor client also sends `0x0504` (same payload
  both to pause and to resume); it is not needed.
- **Seek**: send `0x0507` (stop) and then a new `0x0502` with the new range. Sending a second
  `0x0502` without the stop is ignored. Sequence numbers start from 1 again.
- `0x050c` (no payload), sent by the vendor client after starting a stream, is not needed.

## Downloads

A download is a playback with session id `0x64` and `w8 = 1` in `0x0502`, acknowledged without
pacing. A 30.5-second window arrived in 4.4 seconds (7x) and held exactly the same 763 video
frames as the AVI the vendor SDK produced for that window. The device sends the same media
messages; muxing them into a file is up to the client. The stream ends with the kind 9 message.

## Not yet captured

Removing one channel from a running live set, several playbacks on one connection (and whether
they need different session ids), slow motion and reverse playback, what the device does when a
session id or range is invalid, record types in the segment list, the error code of a rejected
login, PTZ, configuration, alarm events, talk-back, and whether any of this differs between
firmware versions.
