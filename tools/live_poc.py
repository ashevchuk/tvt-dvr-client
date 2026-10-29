#!/usr/bin/env python3
"""Live video from a TVT DVR over a raw TCP socket, without the vendor SDK.

Usage: live_poc.py HOST PORT CHANNELS SECONDS OUT_PREFIX [--sub]

CHANNELS is a comma separated list such as 0,1,2,3; all of them share one connection. The video
of each channel is written to OUT_PREFIX_chN.h264. Credentials come from DVR_USER and DVR_PASS.
See docs/protocol.md.
"""
import collections
import os
import sys
import time

import tvtproto as proto


def main():
    args = [a for a in sys.argv[1:] if a != "--sub"]
    if len(args) != 5:
        sys.exit(__doc__)
    host, port, channels, seconds, prefix = args[0], int(args[1]), [int(c) for c in args[2].split(",")], float(args[3]), args[4]
    try:
        sock, reader = proto.connect(host, port, os.environ["DVR_USER"].encode(), os.environ["DVR_PASS"].encode())
    except PermissionError as error:
        sys.exit(str(error))
    proto.start_live(sock, channels, "--sub" in sys.argv)

    outputs = {c: open("%s_ch%d.h264" % (prefix, c), "wb") for c in channels}
    video, audio = collections.Counter(), collections.Counter()
    started = last_keepalive = time.time()
    while time.time() - started < seconds:
        if time.time() - last_keepalive >= proto.KEEPALIVE_SECONDS:
            proto.send_keepalive(sock)
            last_keepalive = time.time()
        message = reader.read()
        if not message or message[0] != proto.CMD_MEDIA or len(message[1]) < proto.FRAME_HEADER_BYTES:
            continue
        frame = proto.Frame(message[1])
        if frame.channel not in outputs:
            continue
        if frame.kind == proto.KIND_VIDEO:
            video[frame.channel] += 1
            outputs[frame.channel].write(frame.data)
        elif frame.kind == proto.KIND_AUDIO:
            audio[frame.channel] += 1
    proto.stop_live(sock)
    sock.close()
    for c in channels:
        outputs[c].close()
        print("channel %d: video=%d audio=%d" % (c, video[c], audio[c]), file=sys.stderr)


if __name__ == "__main__":
    main()
