#!/usr/bin/env python3
"""Minimal live-video client for a TVT DVR over a raw TCP socket, without the vendor SDK.

Usage: live_poc.py HOST PORT CHANNEL SECONDS OUT.h264 [--sub]
Credentials come from DVR_USER and DVR_PASS. See docs/protocol.md.
"""
import os
import socket
import struct
import sys
import time

MAGIC = b"1111"
CMD_LOGIN = 0x0101
CMD_STREAM_START = 0x0201
CMD_STREAM_STOP = 0x0203
CMD_LOGIN_OK = 0x00010001
CMD_MEDIA = 0x0A000001
KEEPALIVE_SECONDS = 7.5
FRAME_HEADER_BYTES = 60
KIND_VIDEO = 1
NAME_FIELD_BYTES = 36


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("connection closed by the device")
        buf += chunk
    return buf


def send_message(sock, cmd, payload=b""):
    body = struct.pack("<4I", cmd, 0, 4, len(payload)) + payload
    sock.sendall(MAGIC + struct.pack("<I", len(body)) + body)


class Reader:
    """Reads framed messages and reassembles PACK fragments into whole messages."""

    def __init__(self, sock):
        self.sock = sock
        self.fragments = b""

    def read(self):
        """Returns (cmd, payload) of the next complete message, or None while a message is partial."""
        if recv_exact(self.sock, 4) != MAGIC:
            raise ValueError("bad frame magic")
        (length,) = struct.unpack("<I", recv_exact(self.sock, 4))
        body = recv_exact(self.sock, length)
        if body[:4] == b"PACK":
            _, _version, count, _total, index, chunk_len, _zero = struct.unpack("<4s6I", body[:28])
            self.fragments += body[28:28 + chunk_len]
            if index != count:
                return None
            body, self.fragments = self.fragments, b""
        if len(body) < 16:
            return None
        cmd, _a, _b, payload_len = struct.unpack("<4I", body[:16])
        return cmd, body[16:16 + payload_len]


def login(sock, reader, user, password):
    request = struct.pack("<II36s36s36sI", 1, 0, user, password, b"", 4)
    send_message(sock, CMD_LOGIN, request)
    while True:
        message = reader.read()
        if message:
            return message[0] == CMD_LOGIN_OK


def start_stream(sock, channel, sub):
    mask = 1 << channel
    main_mask, sub_mask = (0, mask) if sub else (mask, 0)
    send_message(sock, CMD_STREAM_START, struct.pack("<9I", 0, main_mask, 0, sub_mask, 0, 0, 0, mask, 0))


def main():
    args = [a for a in sys.argv[1:] if a != "--sub"]
    if len(args) != 5:
        sys.exit(__doc__)
    host, port, channel, seconds, out_path = args[0], int(args[1]), int(args[2]), float(args[3]), args[4]
    user = os.environ["DVR_USER"].encode()
    password = os.environ["DVR_PASS"].encode()

    sock = socket.create_connection((host, port), timeout=10)
    if recv_exact(sock, 64)[:4] != b"head":
        sys.exit("unexpected greeting from the device")
    reader = Reader(sock)
    if not login(sock, reader, user, password):
        sys.exit("login rejected")
    start_stream(sock, channel, "--sub" in sys.argv)

    video_frames = key_frames = audio_frames = 0
    started = last_keepalive = time.time()
    with open(out_path, "wb") as out:
        while time.time() - started < seconds:
            if time.time() - last_keepalive >= KEEPALIVE_SECONDS:
                sock.sendall(MAGIC + struct.pack("<I", 0))
                last_keepalive = time.time()
            message = reader.read()
            if not message or message[0] != CMD_MEDIA or len(message[1]) < FRAME_HEADER_BYTES:
                continue
            payload = message[1]
            key, kind, length = struct.unpack("<3I", payload[:12])
            if kind == KIND_VIDEO:
                video_frames += 1
                key_frames += 1 if key else 0
                out.write(payload[FRAME_HEADER_BYTES:FRAME_HEADER_BYTES + length])
            elif kind == 2:
                audio_frames += 1
    send_message(sock, CMD_STREAM_STOP, bytes(36))
    sock.close()
    print("video=%d key=%d audio=%d" % (video_frames, key_frames, audio_frames), file=sys.stderr)


if __name__ == "__main__":
    main()
