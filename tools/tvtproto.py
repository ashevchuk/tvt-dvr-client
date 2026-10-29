"""Wire protocol of a TVT DVR on TCP 6036, as far as it has been reverse engineered.

See docs/protocol.md. Everything here was derived from captured traffic.
"""
import socket
import struct
import time

MAGIC = b"1111"
HEAD_BYTES = 64
FRAME_HEADER_BYTES = 60
KEEPALIVE_SECONDS = 7.5
NAME_FIELD_BYTES = 36
FIRST_PLAYBACK_SESSION = 0xA     # the vendor client numbers playback sessions 0xa, 0xb, 0xc, 0xd
DOWNLOAD_SESSION = 0x64

CMD_LOGIN = 0x0101
CMD_STREAM_START = 0x0201
CMD_STREAM_ADD = 0x0202
CMD_STREAM_STOP = 0x0203
CMD_SEARCH = 0x0501
CMD_PLAY_START = 0x0502
CMD_PLAY_STOP = 0x0507
CMD_PLAY_ACK = 0x050B
CMD_LOGIN_OK = 0x00010001
CMD_SEARCH_REPLY = 0x00050001
CMD_MEDIA = 0x0A000001

KIND_VIDEO, KIND_AUDIO, KIND_END = 1, 2, 9
ALL_RECORD_TYPES = 0xFFFFFFFF
PLAYBACK_RECORD_TYPES = 0xF


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


def send_keepalive(sock):
    sock.sendall(MAGIC + struct.pack("<I", 0))


class Reader:
    """Reads framed messages and reassembles PACK fragments into whole messages."""

    def __init__(self, sock):
        self.sock = sock
        self.fragments = b""

    def read(self):
        """Returns (cmd, payload) of the next complete message, or None while one is partial."""
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


def read_until(reader, wanted_cmd):
    while True:
        message = reader.read()
        if message and message[0] == wanted_cmd:
            return message[1]


def connect(host, port, user, password):
    """Connects and logs in. Returns (socket, reader); raises PermissionError if rejected."""
    sock = socket.create_connection((host, port), timeout=10)
    if recv_exact(sock, HEAD_BYTES)[:4] != b"head":
        raise ConnectionError("unexpected greeting from the device")
    reader = Reader(sock)
    send_message(sock, CMD_LOGIN, struct.pack("<II36s36s36sI", 1, 0, user, password, b"", 4))
    while True:
        message = reader.read()
        if message:
            if message[0] != CMD_LOGIN_OK:
                raise PermissionError("login rejected")
            return sock, reader


def set_live_channels(sock, channels, sub=False, running=False):
    """Selects the set of live channels carried by the connection.

    The first request is a start; later ones replace the whole set, so a smaller mask drops
    channels. An empty set stops everything.
    """
    if not channels:
        stop_live(sock)
        return
    mask = 0
    for channel in channels:
        mask |= 1 << channel
    main_mask, sub_mask = (0, mask) if sub else (mask, 0)
    cmd = CMD_STREAM_ADD if running else CMD_STREAM_START
    send_message(sock, cmd, struct.pack("<9I", 0, main_mask, 0, sub_mask, 0, 0, 0, mask, 0))


def start_live(sock, channels, sub=False):
    """Starts several channels, one request each, the way the vendor client does."""
    for i in range(len(channels)):
        set_live_channels(sock, channels[:i + 1], sub, running=i > 0)


def stop_live(sock):
    send_message(sock, CMD_STREAM_STOP, bytes(36))


def search_days(sock, reader):
    """Returns the start of each recorded day as a Unix time."""
    send_message(sock, CMD_SEARCH, bytes(44))
    payload = read_until(reader, CMD_SEARCH_REPLY)
    return [struct.unpack("<2I", payload[o:o + 8])[1] for o in range(16, len(payload) - 7, 8)]


def search_segments(sock, reader, channel, start, stop):
    """Returns [(start, stop)] of the recorded segments of one channel, as Unix times."""
    mask = 1 << channel
    send_message(sock, CMD_SEARCH, struct.pack("<11I", 1, 0, mask, 0, mask, 0, start, stop, 0, ALL_RECORD_TYPES, 0))
    payload = read_until(reader, CMD_SEARCH_REPLY)
    return [struct.unpack("<2I", payload[o + 4:o + 12]) for o in range(16, len(payload) - 11, 12)]


def start_playback(sock, session, channel, start, stop, download=False):
    """Starts sending recorded frames of one channel. A download is a playback with its own
    session id and a flag."""
    mask = 1 << channel
    send_message(sock, CMD_PLAY_START,
                 struct.pack("<11I", 0xFFFFFFFF, session, mask, 0, mask, 0, start, stop, int(download),
                             PLAYBACK_RECORD_TYPES, 0))


def ack_playback(sock, session, sequence):
    """Tells the device that the session's messages up to `sequence` (counted from 1) were consumed."""
    send_message(sock, CMD_PLAY_ACK, struct.pack("<11I", 0, session, 0, 0, 0, 0, 0, 0, sequence, 0, 0))


def stop_playback(sock, session):
    send_message(sock, CMD_PLAY_STOP, struct.pack("<11I", 0, session, 0, 0, 0, 0, 0, 0, 0, 0, 0))


class Frame:
    """A media message: 60-byte header followed by the data."""

    def __init__(self, payload):
        words = struct.unpack("<15I", payload[:FRAME_HEADER_BYTES])
        self.key = bool(words[0])
        self.kind = words[1]
        self.width, self.height = words[3], words[4]
        self.channel = words[6]
        self.time_us = (words[12] << 32) | words[11]
        self.data = payload[FRAME_HEADER_BYTES:FRAME_HEADER_BYTES + words[2]]


def local_to_unix(text):
    """Parses 'YYYY-MM-DDTHH:MM:SS' as local time."""
    return int(time.mktime(time.strptime(text, "%Y-%m-%dT%H:%M:%S")))
