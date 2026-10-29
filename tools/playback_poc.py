#!/usr/bin/env python3
"""Search and play back recordings from a TVT DVR without the vendor SDK.

  playback_poc.py HOST PORT days
  playback_poc.py HOST PORT segments CHANNEL YYYY-MM-DD
  playback_poc.py HOST PORT play CHANNEL START END SECONDS OUT.h264 [--ack none|immediate|paced] [--speed N]
  playback_poc.py HOST PORT download CHANNEL START END OUT.h264

START and END are local times like 2026-10-29T00:13:16. Credentials come from DVR_USER and
DVR_PASS. The device sends recorded frames only as fast as they are acknowledged, so playback
speed is chosen by how fast the client acknowledges (see docs/protocol.md).
"""
import argparse
import os
import sys
import time

import tvtproto as proto


def show_time(unix):
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(unix))


def play(sock, reader, args):
    start, stop = proto.local_to_unix(args.start), proto.local_to_unix(args.end)
    proto.start_playback(sock, proto.FIRST_PLAYBACK_SESSION, args.channel, start, stop)
    sock.settimeout(5)
    began = last_keepalive = time.time()
    first_us = None
    sequence = video = audio = 0
    last_us = 0
    log = []
    with open(args.out, "wb") as out:
        while time.time() - began < args.seconds:
            if time.time() - last_keepalive >= proto.KEEPALIVE_SECONDS:
                proto.send_keepalive(sock)
                last_keepalive = time.time()
            try:
                message = reader.read()
            except TimeoutError:
                print("no data for 5 s after %d messages: the device stopped sending" % sequence, file=sys.stderr)
                break
            if not message or message[0] != proto.CMD_MEDIA or len(message[1]) < proto.FRAME_HEADER_BYTES:
                continue
            frame = proto.Frame(message[1])
            if frame.kind == proto.KIND_END:
                print("end of the recorded range", file=sys.stderr)
                break
            if frame.kind not in (proto.KIND_VIDEO, proto.KIND_AUDIO):
                continue
            sequence += 1
            if first_us is None:
                first_us = frame.time_us
            last_us = frame.time_us
            if frame.kind == proto.KIND_VIDEO:
                video += 1
                out.write(frame.data)
            else:
                audio += 1
            if args.ack == "paced":
                due = began + (frame.time_us - first_us) / 1e6 / args.speed
                time.sleep(max(0.0, due - time.time()))
            if args.ack != "none":
                proto.ack_playback(sock, proto.FIRST_PLAYBACK_SESSION, sequence)
    proto.stop_playback(sock, proto.FIRST_PLAYBACK_SESSION)
    elapsed = time.time() - began
    print("ack=%s: %d video + %d audio messages in %.1f s" % (args.ack, video, audio, elapsed))
    if first_us is not None:
        print("recording time covered: %.1f s (%s .. %s), %.2fx real time" % (
            (last_us - first_us) / 1e6, show_time(first_us / 1e6), show_time(last_us / 1e6),
            (last_us - first_us) / 1e6 / elapsed))


ACK_EVERY = 8


def download(sock, reader, args):
    """Fetches a period as fast as the device allows; the video goes to a raw .h264 file."""
    start, stop = proto.local_to_unix(args.start), proto.local_to_unix(args.end)
    proto.start_playback(sock, proto.DOWNLOAD_SESSION, args.channel, start, stop, download=True)
    sock.settimeout(10)
    began = last_keepalive = time.time()
    sequence = video = audio = 0
    first_us = last_us = None
    with open(args.out, "wb") as out:
        while True:
            if time.time() - last_keepalive >= proto.KEEPALIVE_SECONDS:
                proto.send_keepalive(sock)
                last_keepalive = time.time()
            try:
                message = reader.read()
            except TimeoutError:
                break
            if not message or message[0] != proto.CMD_MEDIA or len(message[1]) < proto.FRAME_HEADER_BYTES:
                continue
            frame = proto.Frame(message[1])
            if frame.kind == proto.KIND_END:
                break
            if frame.kind not in (proto.KIND_VIDEO, proto.KIND_AUDIO):
                continue
            sequence += 1
            if sequence % ACK_EVERY == 0:
                proto.ack_playback(sock, proto.DOWNLOAD_SESSION, sequence)
            first_us = first_us or frame.time_us
            last_us = frame.time_us
            if frame.kind == proto.KIND_VIDEO:
                video += 1
                out.write(frame.data)
            else:
                audio += 1
    proto.stop_playback(sock, proto.DOWNLOAD_SESSION)
    elapsed = time.time() - began
    print("downloaded %d video + %d audio messages covering %.1f s of recording in %.1f s (%.1fx)" % (
        video, audio, (last_us - first_us) / 1e6, elapsed, (last_us - first_us) / 1e6 / elapsed))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("days")
    seg = sub.add_parser("segments")
    seg.add_argument("channel", type=int)
    seg.add_argument("date")
    pl = sub.add_parser("play")
    pl.add_argument("channel", type=int)
    pl.add_argument("start")
    pl.add_argument("end")
    pl.add_argument("seconds", type=float)
    pl.add_argument("out")
    pl.add_argument("--ack", choices=("none", "immediate", "paced"), default="paced")
    pl.add_argument("--speed", type=float, default=1.0)
    dl = sub.add_parser("download")
    dl.add_argument("channel", type=int)
    dl.add_argument("start")
    dl.add_argument("end")
    dl.add_argument("out")
    args = parser.parse_args()

    sock, reader = proto.connect(args.host, args.port, os.environ["DVR_USER"].encode(), os.environ["DVR_PASS"].encode())
    try:
        if args.command == "days":
            for day in proto.search_days(sock, reader):
                print(show_time(day)[:10])
        elif args.command == "segments":
            day = proto.local_to_unix(args.date + "T00:00:00")
            segments = proto.search_segments(sock, reader, args.channel, day, day + 86399)
            for start, stop in segments:
                print(show_time(start), "..", show_time(stop)[11:])
            print("%d segments" % len(segments))
        elif args.command == "download":
            download(sock, reader, args)
        else:
            play(sock, reader, args)
    finally:
        sock.close()


if __name__ == "__main__":
    main()
