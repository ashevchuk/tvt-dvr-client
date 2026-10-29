"""Transparent TCP proxy that logs framed messages. The password is masked before anything is written."""
import os, socket, struct, sys, threading, time

LISTEN, DEV = ("127.0.0.1", 16036), (os.environ["TAP_DEVICE"], 6036)
SECRET = os.environ["DVR_PASS"].encode()
KEEP = int(os.environ.get("TAP_KEEP", "160"))     # payload bytes kept per message
out = open(sys.argv[1], "w")
lock = threading.Lock()
t0 = time.time()

def mask(b):
    return b.replace(SECRET, b"*" * len(SECRET)) if SECRET else b

def log(cid, tag, kind, body):
    with lock:
        out.write("%.3f c%d %s %s len=%d\n  %s\n" % (time.time() - t0, cid, tag, kind, len(body), mask(body[:KEEP]).hex()))
        out.flush()

def pump(src, dst, tag, cid):
    buf = b""
    first = True
    try:
        while True:
            d = src.recv(65536)
            if not d:
                break
            dst.sendall(d)
            buf += d
            while True:
                if first and tag == "S>C":
                    if len(buf) < 64: break
                    log(cid, tag, "HEAD", buf[:64]); buf = buf[64:]; first = False; continue
                if len(buf) < 8: break
                magic, n = struct.unpack("<4sI", buf[:8])
                if magic != b"1111":
                    log(cid, tag, "RAW", buf); buf = b""; break
                if len(buf) < 8 + n: break
                log(cid, tag, "MSG", buf[8:8 + n]); buf = buf[8 + n:]; first = False
    except OSError:
        pass
    finally:
        for s in (src, dst):
            try: s.shutdown(socket.SHUT_RDWR)
            except OSError: pass

srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(LISTEN); srv.listen(8); srv.settimeout(float(sys.argv[2]))
cid = 0
try:
    while True:
        c, _ = srv.accept()
        d = socket.create_connection(DEV)
        cid += 1
        for a, b, t in ((c, d, "C>S"), (d, c, "S>C")):
            threading.Thread(target=pump, args=(a, b, t, cid), daemon=True).start()
except socket.timeout:
    pass
