# Wire protocol (TCP 6036)

What the vendor SDK and the web client exchange with the DVR, reconstructed from LAN traffic
captured with `tools/tap.py` on one 16-channel device (firmware `3.3.0.P-3531-00`). Everything
here was observed on the wire; nothing was taken from vendor code. All integers are
little-endian. Items marked *(hypothesis)* fit the captures but have not been tested directly,
and anything not listed is unknown.

`tools/live_poc.py` and `tools/playback_poc.py` are working clients for the parts below. The
results were checked against the manufacturer's own SDK on the same device (frame counts of a
download, search results, playback timing).

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
with four live tiles. The first request is `0x0201`; every later request is `0x0202`. Both carry
the **complete set** of active channels, not a change: `0x0202` with `0x3` and then `0x7` and
`0xf` grows the set, and `0xd`, `0x9`, `0x1` shrink it again (the vendor client does exactly this
when the layout goes from four tiles to one). `0x0203` with zeros stops everything.

The main-stream mask (`w1`) and the sub-stream mask (`w3`) are independent: main on some
channels and sub on others in one request works, a channel can move between the streams, and
even both streams of one channel can run together. Media messages are told apart by the channel
number (`w6`) and the stream (`w9`, see below).

## Media messages

Streaming starts with the replies to `0x0201` and then `cmd = 0x0a000001` messages. The payload
is a 60-byte frame header (fifteen words) followed by `w2` bytes of data.

| word | meaning |
|---|---|
| w0 | `1` on key frames and audio frames, `0` on other video frames |
| w1 | kind: `1` video, `2` audio, `3` video format, `4` audio format, `9` end of data (no data, only in playback and downloads); `7` and `8` only on a talk-back session, see below |
| w2 | length of the data after the header |
| w3, w4 | width and height (video and video format only) |
| w6 | channel number, counting from 0 |
| w9 | stream: `0x20` main stream (and all audio), `0x40` sub stream |
| w5 | an address inside the device; not data |
| w11, w12 | low and high 32 bits of a timestamp in microseconds since the Unix epoch (UTC) |
| w13 | a second microsecond counter, advancing in step with w11 *(hypothesis: time since stream start)* |
| w14 | `0x108` in all captures |

Other words (w7, w8, w10) were constant or not understood.

- **Video** (kind 1): Annex-B H.264, SPS/PPS inside key frames; consecutive frames are 40 ms
  apart at 25 fps. The same frames the SDK hands to its callback.
- **Audio** (kind 2): 840 bytes, five 168-byte units of a 4-byte header and a 164-byte IMA ADPCM
  block, as described in [device-notes.md](device-notes.md); 5 messages per second.
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
video. A playback request covers one channel. The first frame is the key frame at or before the
requested start, so it can be a few seconds early or even in an earlier segment. A session
started inside a gap begins at that channel's next recording. When the range ends the device
sends a media message of kind 9.

### Several playbacks on one connection

The vendor client plays up to four recordings over one connection, one session each, numbered
`0x0a`, `0x0b`, `0x0c`, `0x0d` by tile. Each session has its own channel mask in `0x0502`, its
own sequence numbers and acknowledgements (the session id is in `w1` of `0x050b` and `0x0507`)
and its own kind 9 end marker; the media messages are told apart by the channel number. The
recorded segments are searched per channel with one `0x0501` each. Four sessions at once is the
device's limit. Verified with channels 0 to 3 played in parallel, each acknowledged by its own
timestamps. Using a session number outside this range, or two sessions for one channel, was not
tried.

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

## Talk-back

Talk-back sends audio from the client to the device, which plays it on its audio output. It
works on a connection that has logged in; the client uses a separate connection for it.

| message | direction | payload |
|---|---|---|
| `0x0b01` | to device | empty: start talking |
| `0x000b0001` | from device | empty: accepted |
| `0x0b02` | to device | one media-style message, see below |
| `0x0b03` | to device | empty: stop; no reply |

A `0x0b02` payload is a 60-byte frame header followed by the audio. In the header `w1 = 7` (kind:
talk audio) and `w2` is the length of the audio; the other words are `0` and the device accepts
that. The audio is IMA ADPCM in the same shape as live audio: 168-byte units of the header
`00 01 52 00` and a 164-byte block (321 samples, 8 kHz mono). The client sends five units (840
bytes, 200 ms) per message, in real time; the keepalive of the connection continues as usual.
Messages with other numbers of units were not tried.

After `0x000b0001` the device sends `0x0a000001` messages on the same connection: one with
`w1 = 8` and 18 data bytes, a `WAVEFORMATEX` whose format tag is `0x11` (IMA ADPCM) with one
channel, and then messages with `w1 = 7` carrying 840 bytes of audio in the same unit shape, about
every 200 ms. What that audio contains was not examined. A reply refusing the request was not
observed.

## Not yet captured

Slow motion and reverse playback, what the device does when a session id or range is invalid,
record types in the segment list, the error code of a rejected login, PTZ, configuration, alarm
events, the content of the audio the device sends during talk-back, and whether any of this
differs between firmware versions.
