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
| `0x0203` | 36 zero bytes | stop streaming (the vendor client sends this before closing) |

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

Verified for the main stream on channels 0 and 5 and for the sub stream on channels 0, 3 and 5,
one channel per request. Requesting several channels by setting several bits, or both streams
at once, was not tried.

## Media messages

Streaming starts with the replies to `0x0201` and then `cmd = 0x0a000001` messages. The payload
is a 60-byte frame header (fifteen words) followed by `w2` bytes of data.

| word | meaning |
|---|---|
| w0 | `1` on key frames and audio frames, `0` on other video frames |
| w1 | kind: `1` video, `2` audio, `3` video format, `4` audio format |
| w2 | length of the data after the header |
| w3, w4 | width and height (video and video format only) |
| w5 | an address inside the device; not data |
| w11, w12 | low and high 32 bits of a timestamp in microseconds since the Unix epoch (UTC) |
| w13 | a second microsecond counter, advancing in step with w11 *(hypothesis: time since stream start)* |
| w14 | `0x108` in all captures |

Other words (w6-w10) were constant or not understood.

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

## Not yet captured

Playback (search by date, search by time, play, pause, speed, seek), downloading, PTZ,
configuration, alarm events, talk-back, how the device reports a rejected stream request,
and whether any of the above differs between firmware versions.
