#!/usr/bin/env python3
# ws_q.py '<lua>' — send one line, print matron's reply (strip ws framing)
import socket, base64, os, sys, time

host, port = "192.168.1.139", 5555
key = base64.b64encode(os.urandom(16)).decode()
req = ("GET / HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n"
       "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
       "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: bus.sp.nanomsg.org\r\n\r\n"
       % (host, port, key))
s = socket.create_connection((host, port), timeout=5)
s.sendall(req.encode())
resp = b""
while b"\r\n\r\n" not in resp:
    resp += s.recv(4096)
assert "101" in resp.split(b"\r\n", 1)[0].decode()

def send_text(sock, payload):
    p = payload.encode(); mask = os.urandom(4)
    header = bytearray([0x81]); n = len(p)
    if n < 126: header.append(0x80 | n)
    elif n < 65536: header.append(0x80 | 126); header += n.to_bytes(2, "big")
    else: header.append(0x80 | 127); header += n.to_bytes(8, "big")
    header += mask
    sock.sendall(bytes(header) + bytes(b ^ mask[i % 4] for i, b in enumerate(p)))

def read_frames(sock, timeout=2.0):
    sock.settimeout(timeout)
    out = b""
    try:
        while True:
            h = sock.recv(2)
            if not h: break
            n = h[1] & 0x7f
            if n == 126: n = int.from_bytes(sock.recv(2), "big")
            elif n == 127: n = int.from_bytes(sock.recv(8), "big")
            payload = b""
            while len(payload) < n:
                payload += sock.recv(n - len(payload))
            out += payload
    except socket.timeout:
        pass
    return out.replace(b"\x00", b"").decode(errors="replace").strip()

for line in sys.argv[1:]:
    send_text(s, line + "\n")
    time.sleep(0.2)
    print(line, "=>", repr(read_frames(s)))
s.close()
