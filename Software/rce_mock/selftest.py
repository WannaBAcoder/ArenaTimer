#!/usr/bin/env python3
"""Exercises mock_rce_server.py the way a hardware timer would (stdlib only).

Doubles as the reference "device loop" in executable form: if the mock and a
firmware build disagree, this client shows which side deviates from the docs.
Usage: python selftest.py [--port 8765] [--admin-port 8766] [--key ...]
Start the mock first (python mock_rce_server.py).
"""
import argparse
import base64
import json
import os
import socket
import struct
import sys
import time
import urllib.parse
import urllib.request

ARENA = "Arena 1"


class Client:
    def __init__(self, host, port, key, device, arenas=(ARENA,), header_auth=True):
        self.s = socket.create_connection((host, port), timeout=5)
        k = base64.b64encode(os.urandom(16)).decode()
        path = "/cable" + ("" if header_auth else f"?key={key}")
        req = (f"GET {path} HTTP/1.1\r\nHost: {host}:{port}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {k}\r\nSec-WebSocket-Version: 13\r\n"
               "Sec-WebSocket-Protocol: actioncable-v1-json\r\n")
        if header_auth:
            req += f"Authorization: Bearer {key}\r\n"
        self.s.sendall((req + "\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += self.s.recv(1)
        assert b" 101 " in buf.split(b"\r\n")[0], buf
        self.device, self.arenas = device, list(arenas)
        self.ident = json.dumps({"channel": "IntegrationChannel", "arenas": self.arenas, "device": device},
                                separators=(",", ":"))
        self.cmd_n = int.from_bytes(os.urandom(2), "big")

    def _send(self, obj):
        data = json.dumps(obj, separators=(",", ":")).encode()
        mask = os.urandom(4)
        n = len(data)
        head = bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0x80 | 126]) + struct.pack("!H", n))
        self.s.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _exact(self, n):
        out = b""
        while len(out) < n:
            chunk = self.s.recv(n - len(out))
            if not chunk:
                raise ConnectionError("closed")
            out += chunk
        return out

    def recv(self, timeout=5):
        self.s.settimeout(timeout)
        h = self._exact(2)
        n = h[1] & 0x7F
        if n == 126:
            n = struct.unpack("!H", self._exact(2))[0]
        elif n == 127:
            n = struct.unpack("!Q", self._exact(8))[0]
        return json.loads(self._exact(n).decode())

    def recv_until(self, pred, timeout=6):
        end = time.time() + timeout
        while time.time() < end:
            try:
                f = self.recv(max(0.1, end - time.time()))
            except socket.timeout:
                break
            if pred(f):
                return f
        raise AssertionError("timed out waiting for frame")

    def subscribe(self):
        self._send({"command": "subscribe", "identifier": self.ident})

    def command(self, command, **kw):
        self.cmd_n += 1
        d = {"action": "timer", "arena": ARENA, "command": command, "id": f"{self.device}-{self.cmd_n}"}
        d.update(kw)
        self._send({"command": "message", "identifier": self.ident, "data": json.dumps(d)})
        return d["id"]


def admin(port, **q):
    qs = urllib.parse.urlencode(q)
    with urllib.request.urlopen(f"http://127.0.0.1:{port}/admin?{qs}", timeout=3) as r:
        return json.loads(r.read())["result"]


def msg(f):
    return f.get("message", {})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--admin-port", type=int, default=8766)
    ap.add_argument("--key", default="rcm_test_mockkey0000000000000000")
    a = ap.parse_args()
    ok = True

    def check(name, cond):
        nonlocal ok
        print(("PASS  " if cond else "FAIL  ") + name)
        ok &= bool(cond)

    # 1. bad key -> unauthorized, no retry
    c = Client(a.host, a.port, "rcm_live_wrong", "T0")
    f = c.recv()
    check("bad key -> disconnect/unauthorized, reconnect=false",
          f.get("type") == "disconnect" and f.get("reason") == "unauthorized" and f.get("reconnect") is False)

    # 2. handshake
    admin(a.admin_port, cmd="uncall"); admin(a.admin_port, cmd="authority", value="rce")
    c = Client(a.host, a.port, a.key, "ArenaTimer A1")
    check("welcome first", c.recv().get("type") == "welcome")
    c.subscribe()
    snap = c.recv_until(lambda f: msg(f).get("type") == "snapshot")
    conf = c.recv_until(lambda f: f.get("type") == "confirm_subscription")
    check("snapshot then confirm_subscription", snap and conf and conf["identifier"] == c.ident)
    arena = msg(snap)["data"]["arenas"][0]
    check("snapshot has timer=null with no match, authority rce_manager",
          arena["timer"] is None and arena["authority"]["type"] == "rce_manager")

    # 3. match called
    admin(a.admin_port, cmd="call")
    f = c.recv_until(lambda f: msg(f).get("type") == "match.called")
    t = msg(f)["data"]["timer"]
    check("match.called carries idle timer 180s", t["state"] == "idle" and t["remaining_s"] == 180)

    # 4. start (server keeps time)
    cid = c.command("start", version=t["version"])
    ack = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("start -> ack", msg(ack)["type"] == "ack" and msg(ack)["data"]["timer"]["state"] == "running")
    bc = c.recv_until(lambda f: msg(f).get("type") == "timer.started")
    check("timer.started broadcast with ends_at and integration source",
          msg(bc)["data"]["timer"]["ends_at"] and msg(bc)["data"]["timer"]["last_source"]["device"] == "ArenaTimer A1")
    ver = msg(bc)["data"]["timer"]["version"]

    # 5. duplicate id, stale version, already running
    c._send({"command": "message", "identifier": c.ident, "data": json.dumps(
        {"action": "timer", "arena": ARENA, "command": "start", "id": cid})})
    d = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("duplicate id -> ack duplicate=true", msg(d)["type"] == "ack" and msg(d)["data"]["duplicate"] is True)
    c.command("pause", version=ver - 1)
    e = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("old version -> stale_version with timer", msg(e)["type"] == "error" and msg(e)["data"]["error"] == "stale_version"
          and msg(e)["data"]["timer"])
    c.command("start")
    e = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("start while running -> already_running", msg(e)["data"].get("error") == "already_running")
    c.command("adjust", remaining_s=60)
    e = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("adjust while running (not authority) -> timer_running", msg(e)["data"].get("error") == "timer_running")

    # 6. tablet pause arrives as timer.paused
    admin(a.admin_port, cmd="pause")
    f = c.recv_until(lambda f: msg(f).get("type") == "timer.paused")
    check("tablet pause -> timer.paused, source arena_manager",
          msg(f)["data"]["timer"]["last_source"]["type"] == "arena_manager")

    # 7. device authority
    admin(a.admin_port, cmd="authority", value="ArenaTimer A1")
    f = c.recv_until(lambda f: msg(f).get("type") == "queue.updated")
    check("queue.updated carries authority=device", msg(f)["data"]["arena"]["authority"]["device"] == "ArenaTimer A1")
    c.command("start", remaining_s=101)
    ack = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("authority start uses device remaining_s (101)", msg(ack)["data"]["timer"]["remaining_s"] == 101)
    c.command("adjust", remaining_s=95)
    ack = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("authority may adjust while running", msg(ack)["type"] == "ack" and msg(ack)["data"]["timer"]["remaining_s"] == 95)
    c.command("pause", remaining_s=0)
    c.recv_until(lambda f: msg(f).get("type") == "timer.finished")
    check("authority pause at 0 -> timer.finished", True)

    # 8. reset, then ping cadence
    c.command("reset")
    ack = c.recv_until(lambda f: msg(f).get("type") in ("ack", "error"))
    check("reset -> idle at full time", msg(ack)["data"]["timer"]["state"] == "idle"
          and msg(ack)["data"]["timer"]["remaining_s"] == 180)
    f = c.recv_until(lambda f: f.get("type") == "ping", timeout=4)
    check("ping frame within 3-4 s", f.get("type") == "ping")

    print("\nALL PASSED" if ok else "\nFAILURES")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
