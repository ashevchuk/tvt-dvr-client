# Device notes

Observations from a 16-channel DVR (firmware `3.3.0.P-3531-00`, 4 audio inputs). They explain
choices made in the client; the message formats are in [protocol.md](protocol.md). None of this
is manufacturer documentation.

## Connection

- The device listens on TCP 6036 (the web UI's `server.js` says `NetPort=6036`).
- One connection carries many streams: 16 live channels, or four playbacks, share it. Further
  connections work too; the client uses one for live view and one per playback and download.
- A login that is rejected does not lock the account (one failed attempt was made).

## Live streams

- Each channel has two streams: main 928x576 and sub 352x288. The sub stream is the main
  stream's field of view squeezed horizontally; display it at 928:576.
- Video arrives as Annex-B H.264 with SPS/PPS in key frames. The first picture waits for a key
  frame, so up to a few seconds after start.
- libavcodec logs `SEI type 18 ... truncated` on nearly every frame; the payload is a vendor
  extension and the picture is unaffected.
- Audio arrives unprompted with the stream; there is nothing to switch on. Only the first four
  channels have audio inputs.
- The audio data is a series of 168-byte units. Each unit is a 4-byte header `00 01 52 00`
  (`0x52` is 82 words, the length of what follows) and one IMA ADPCM block of 164 bytes: a
  4-byte block header (predictor, step index, padding) and 160 bytes of data, 321 samples at
  8 kHz mono. A message holds five units, 200 ms. The format frame claims a block alignment of
  1, which is wrong.
- Decoding 168-byte blocks that start after the unit header looks right but reads the next
  unit's header as 8 extra samples per block; drop the 4-byte header and decode 164-byte blocks.
- The device sends audio in 200 ms bursts, so a ~300 ms jitter buffer is needed.

## Time

Frame timestamps are microseconds since the Unix epoch (UTC). The recorded segments and days
the device reports are local wall-clock times of the device, which the client assumes to be in
the time zone of the computer it runs on.

## Playback

- At most four playbacks can run at once; a fifth is refused.
- A session started in a gap begins at that channel's next recording, which can be much later.
  A session started inside a segment begins at the preceding key frame, possibly in the previous
  segment, so frames earlier than the requested time must be decoded but not shown.
- One session running to the end of the day crosses gaps by itself.
- The device lists a day with recordings more than once around the clock change.
- The list of segments carries no record type.

## Downloads

- A download arrives about seven times faster than real time. Writing it into a Matroska file
  with the device's timestamps gives the exact frame count of the requested period plus the few
  seconds before the first key frame.
- The files the manufacturer's own software produced for the same period were AVI files whose
  header says 29.97 fps and which contain filler slots; the real frame rate is 25.
