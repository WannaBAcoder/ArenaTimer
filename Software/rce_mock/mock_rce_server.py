#!/usr/bin/env python3
"""Mock RCE Manager (live + timer-control v1), for testing hardware timers
without access to the real service.

Implements, from BotCodeBashers/rce-manager-integrations:
  live-v1.md          ActionCable-style WebSocket: welcome, subscribe ->
                      snapshot + confirm_subscription, ping every 3 s,
                      seq-numbered change messages, disconnect/unauthorized.
  timer-control-v1.md start/pause/reset/adjust over the socket, ack/error,
                      idempotency ids, stale_version, device authority.

Standard library only (hand-rolled RFC 6455) so there is nothing to install.
Plain ws:// by default (the ESP32 firmware has a dev setting to skip TLS).

A second port serves a tiny HTTP admin API that plays the part of the Arena
Manager's tablet and the organizer's QR page:

  GET /admin?cmd=call                      call a match to the arena
  GET /admin?cmd=uncall
  GET /admin?cmd=start|pause|reset         tablet button presses
  GET /admin?cmd=adjust&value=90           set remaining seconds
  GET /admin?cmd=duration&value=180        change match length (idle only)
  GET /admin?cmd=authority&value=<device>  organizer assigns a device ("rce"
                                           hands the clock back to the server)
  GET /admin?cmd=skew&value=5              shift the running clock without telling
                                           anyone (to provoke drift correction)
  GET /admin?cmd=kick                      close every device connection
  GET /admin?cmd=silence&value=1|0         stop/resume ping frames
  GET /admin?cmd=refuse&value=1|0          reject new connections (keeps state;
                                           with kick, simulates an outage)
  GET /admin/state                         JSON: arenas, devices, frame log

Everything the server sees/sends is also printed to stdout.
This is a test double written from the docs, not the real service: wherever
the docs are silent the choice made here is marked "UNDOCUMENTED".
"""
import argparse
import asyncio
import base64
import hashlib
import json
import struct
import sys
import time
import urllib.parse
from datetime import datetime, timezone

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
EVENT_ID = 7
FRAME_LOG_MAX = 400


def iso(ts):
    return datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Arena:
    def __init__(self, name):
        self.name = name
        self.match_id = None
        self.next_match_id = 18000
        self.state = "idle"
        self.duration = 180
        self.remaining = 180.0     # valid when not running
        self.end_ts = None         # valid when running
        self.version = 0
        self.last_source = {"type": "system"}
        self.authority = {"type": "rce_manager"}
        self.seen_ids = set()
        self.finish_task = None

    def remaining_now(self):
        if self.state == "running":
            return max(0.0, self.end_ts - time.time())
        return self.remaining

    def timer(self):
        if self.match_id is None:
            return None
        now = time.time()
        rem = self.remaining_now()
        return {
            "match_id": self.match_id,
            "state": self.state,
            "duration_s": self.duration,
            "remaining_s": int(round(rem)),
            "ends_at": iso(self.end_ts) if self.state == "running" else None,
            "server_time": iso(now),
            "version": self.version,
            "last_source": self.last_source,
            "authority": self.authority,
        }

    def match(self):
        if self.match_id is None:
            return None
        return {"id": self.match_id, "state": self.state, "arena": self.name,
                "queue_index": 1}

    def arena_obj(self):
        return {"name": self.name, "progress": {"completed": 0, "total": 4},
                "current": self.match(), "up_next": [], "timer": self.timer(),
                "authority": self.authority}


class Conn:
    def __init__(self, reader, writer, peer):
        self.reader, self.writer, self.peer = reader, writer, peer
        self.identifier = None
        self.arenas = None          # None = all
        self.device = None
        self.open = True

    def wants(self, arena_name):
        return self.identifier is not None and (self.arenas is None or arena_name in self.arenas)


class Server:
    def __init__(self, key, arenas):
        self.key = key
        self.arenas = {n: Arena(n) for n in arenas}
        self.conns = []
        self.devices_seen = set()
        self.seq = 0
        self.silence = False
        self.refuse = False
        self.log = []

    # -- logging ----------------------------------------------------------
    def note(self, direction, peer, text):
        line = f"{time.strftime('%H:%M:%S')} {direction} {peer} {text}"
        print(line, flush=True)
        self.log.append(line)
        del self.log[:-FRAME_LOG_MAX]

    # -- websocket plumbing ----------------------------------------------
    async def send_json(self, c, obj):
        if not c.open:
            return
        data = json.dumps(obj, separators=(",", ":")).encode()
        n = len(data)
        if n < 126:
            head = struct.pack("!BB", 0x81, n)
        elif n < 65536:
            head = struct.pack("!BBH", 0x81, 126, n)
        else:
            head = struct.pack("!BBQ", 0x81, 127, n)
        try:
            c.writer.write(head + data)
            await c.writer.drain()
            self.note("->", c.peer, data.decode()[:300])
        except Exception:
            c.open = False

    async def read_frame(self, c):
        h = await c.reader.readexactly(2)
        opcode = h[0] & 0x0F
        masked = h[1] & 0x80
        n = h[1] & 0x7F
        if n == 126:
            n = struct.unpack("!H", await c.reader.readexactly(2))[0]
        elif n == 127:
            n = struct.unpack("!Q", await c.reader.readexactly(8))[0]
        mask = await c.reader.readexactly(4) if masked else b""
        payload = await c.reader.readexactly(n) if n else b""
        if masked:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        return opcode, payload

    async def handle_ws(self, reader, writer):
        peer = "%s:%s" % writer.get_extra_info("peername")[:2]
        self.note("<-", peer, "TCP connection accepted")
        if self.refuse:
            self.note("!!", peer, "refusing (admin refuse=1)")
            writer.close()
            return
        try:
            first = await asyncio.wait_for(reader.readexactly(1), 10)
            if first[0] == 0x16:   # TLS handshake record
                self.note("!!", peer, "client is speaking TLS (wss://) - uncheck TLS on the timer; this mock is plain ws://")
                writer.close()
                return
            raw = first + await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10)
        except Exception as e:
            self.note("!!", peer, f"no HTTP request received ({type(e).__name__})")
            writer.close()
            return
        lines = raw.decode("latin1").split("\r\n")
        reqline = lines[0]
        headers = {}
        for ln in lines[1:]:
            if ":" in ln:
                k, v = ln.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        path = reqline.split(" ")[1] if " " in reqline else "/"
        qs = urllib.parse.parse_qs(urllib.parse.urlparse(path).query)
        token = None
        auth = headers.get("authorization", "")
        if auth.lower().startswith("bearer "):
            token = auth[7:].strip()
        elif "key" in qs:
            token = qs["key"][0]
        self.note("<-", peer, f"HTTP {reqline} auth={'header' if auth else 'query' if token else 'none'}"
                              f" proto={headers.get('sec-websocket-protocol', '-')}")
        if "sec-websocket-key" not in headers or not path.startswith("/cable"):
            writer.write(b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
            await writer.drain()
            writer.close()
            return
        accept = base64.b64encode(hashlib.sha1((headers["sec-websocket-key"] + GUID).encode()).digest()).decode()
        resp = ("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                f"Sec-WebSocket-Accept: {accept}\r\n")
        if "sec-websocket-protocol" in headers:
            resp += "Sec-WebSocket-Protocol: actioncable-v1-json\r\n"
        writer.write((resp + "\r\n").encode())
        await writer.drain()

        c = Conn(reader, writer, peer)
        self.conns.append(c)
        try:
            if token != self.key:
                await self.send_json(c, {"type": "disconnect", "reason": "unauthorized", "reconnect": False})
                return
            await self.send_json(c, {"type": "welcome"})
            while c.open:
                opcode, payload = await self.read_frame(c)
                if opcode == 8:
                    break
                if opcode == 9:
                    writer.write(b"\x8a" + bytes([len(payload)]) + payload)
                    await writer.drain()
                    continue
                if opcode != 1:
                    continue
                text = payload.decode("utf-8", "replace")
                self.note("<-", peer, text[:300])
                await self.on_client_frame(c, text)
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            c.open = False
            if c in self.conns:
                self.conns.remove(c)
            self.note("--", peer, f"closed (device={c.device})")
            try:
                writer.close()
            except Exception:
                pass

    # -- protocol ---------------------------------------------------------
    def envelope(self, typ, arena, data, seq=True):
        if seq:
            self.seq += 1
        return {"v": 1, "type": typ, "id": f"msg_{self.seq}_{int(time.time()*1000)%100000}",
                "seq": self.seq if seq else None, "event": EVENT_ID,
                "arena": arena, "at": iso(time.time()), "data": data}

    def snapshot_data(self, c):
        names = list(self.arenas) if c.arenas is None else [n for n in c.arenas if n in self.arenas]
        return {"event": {"id": EVENT_ID, "name": "Mock Event", "date": time.strftime("%Y-%m-%d"),
                          "rce_event_id": 1234, "test": True},
                "progress": {"completed": 0, "total": 4},
                "arenas": [self.arenas[n].arena_obj() for n in names]}

    async def send_wrapped(self, c, env):
        await self.send_json(c, {"identifier": c.identifier, "message": env})

    async def broadcast(self, arena, typ, data):
        env = self.envelope(typ, arena, data)
        for c in list(self.conns):
            if c.wants(arena):
                await self.send_wrapped(c, env)

    async def on_client_frame(self, c, text):
        try:
            f = json.loads(text)
        except ValueError:
            return
        cmd = f.get("command")
        if cmd == "subscribe":
            try:
                ident = json.loads(f["identifier"])
                arenas = ident.get("arenas")
                if arenas is not None and (not isinstance(arenas, list) or len(arenas) > 20):
                    raise ValueError
                if ident.get("channel") != "IntegrationChannel":
                    raise ValueError
            except Exception:
                await self.send_json(c, {"identifier": f.get("identifier"), "type": "reject_subscription"})
                return
            c.identifier = f["identifier"]
            c.arenas = arenas
            c.device = ident.get("device")
            if c.device:
                self.devices_seen.add(c.device)
            await self.send_wrapped(c, self.envelope("snapshot", None, self.snapshot_data(c), seq=False) | {"seq": self.seq})
            await self.send_json(c, {"identifier": c.identifier, "type": "confirm_subscription"})
        elif cmd == "message" and c.identifier:
            try:
                data = json.loads(f.get("data", "{}"))
            except ValueError:
                return
            if data.get("action") == "resync":
                await self.send_wrapped(c, self.envelope("snapshot", None, self.snapshot_data(c), seq=False) | {"seq": self.seq})
            elif data.get("action") == "timer":
                await self.on_timer_command(c, data)

    async def reply(self, c, typ, arena, payload):
        await self.send_wrapped(c, self.envelope(typ, arena, payload, seq=False))

    async def on_timer_command(self, c, d):
        cid = d.get("id")
        command = d.get("command")
        arena_name = d.get("arena")

        async def err(code, msg, timer=None):
            await self.reply(c, "error", arena_name,
                             {"command_id": cid, "command": command, "error": code, "message": msg, "timer": timer})

        if not c.device:
            return await err("device_required", "Subscribe with a device name.")
        if command not in ("start", "pause", "reset", "adjust"):
            return await err("invalid_command", "command must be start, pause, reset or adjust")
        a = self.arenas.get(arena_name)
        if a is None:
            return await err("unknown_arena", "Valid arenas: " + ", ".join(self.arenas))
        rem_in = d.get("remaining_s")
        if rem_in is not None and (not isinstance(rem_in, int) or not 0 <= rem_in <= 5999):
            return await err("invalid_command", "remaining_s must be an integer 0-5999")
        if command == "adjust" and rem_in is None:
            return await err("invalid_command", "remaining_s is required for adjust")
        if a.match_id is None:
            return await err("no_match", "No match is called in this arena.")
        t = a.timer()
        if cid and cid in a.seen_ids:
            return await self.reply(c, "ack", arena_name,
                                    {"command_id": cid, "command": command, "duplicate": True, "timer": t})
        ver = d.get("version")
        if ver is not None and ver != a.version:
            return await err("stale_version", "Timer changed since version %s." % ver, t)

        is_auth = a.authority.get("type") == "device" and a.authority.get("device") == c.device
        evt = None
        if command == "start":
            if a.state == "running":
                return await err("already_running", "Timer is already running.", t)
            if a.state == "finished":                      # UNDOCUMENTED
                return await err("match_complete", "Match is complete.", t)
            rem = rem_in if (is_auth and rem_in is not None) else a.remaining_now()
            a.remaining = float(rem)
            a.end_ts = time.time() + rem
            a.state = "running"
            evt = "timer.started"
        elif command == "pause":
            if a.state != "running":
                return await err("not_running", "Timer is not running.", t)
            rem = rem_in if (is_auth and rem_in is not None) else a.remaining_now()
            a.remaining = float(rem)
            a.end_ts = None
            if is_auth and rem_in == 0:
                a.state = "finished"
                evt = "timer.finished"
            else:
                a.state = "paused"
                evt = "timer.paused"
        elif command == "reset":
            a.state = "idle"
            a.remaining = float(a.duration)
            a.end_ts = None
            a.seen_ids.clear()
            evt = "timer.reset"
        elif command == "adjust":
            if a.state == "finished":
                return await err("match_complete", "Match is complete.", t)
            if a.state == "running" and not is_auth:
                return await err("timer_running", "Only the device running the clock can adjust while running.", t)
            a.remaining = float(rem_in)
            if a.state == "running":
                a.end_ts = time.time() + rem_in
            evt = "timer.adjusted"
        if cid:
            a.seen_ids.add(cid)
        a.version += 1
        a.last_source = {"type": "integration", "device": c.device}
        await self.reply(c, "ack", arena_name,
                         {"command_id": cid, "command": command, "duplicate": False, "timer": a.timer()})
        await self.after_change(a, evt)

    async def after_change(self, a, evt):
        await self.broadcast(a.name, evt, {"match": a.match(), "timer": a.timer()})
        if a.finish_task:
            a.finish_task.cancel()
            a.finish_task = None
        if a.state == "running":
            a.finish_task = asyncio.create_task(self.finish_when_done(a, a.end_ts, a.version))

    async def finish_when_done(self, a, end_ts, version):
        try:
            await asyncio.sleep(max(0, end_ts - time.time()))
            if a.state == "running" and a.version == version:
                a.state, a.remaining, a.end_ts = "finished", 0.0, None
                a.version += 1
                a.last_source = {"type": "system"}
                await self.broadcast(a.name, "timer.finished", {"match": a.match(), "timer": a.timer()})
        except asyncio.CancelledError:
            pass

    async def ping_loop(self):
        while True:
            await asyncio.sleep(3)
            if self.silence:
                continue
            for c in list(self.conns):
                await self.send_json(c, {"type": "ping", "message": int(time.time())})

    # -- admin (the tablet / organizer) -----------------------------------
    async def admin(self, cmd, value, arena_name):
        a = self.arenas.get(arena_name) or next(iter(self.arenas.values()))
        if cmd == "call":
            if a.match_id is None:
                a.match_id = a.next_match_id
                a.next_match_id += 1
                a.state, a.remaining, a.end_ts, a.version = "idle", float(a.duration), None, 1
                a.seen_ids.clear()
                a.last_source = {"type": "event_manager"}
                await self.broadcast(a.name, "match.called", {"match": a.match(), "timer": a.timer()})
            return "called"
        if cmd == "uncall":
            if a.match_id is not None:
                m = a.match()
                a.match_id, a.state, a.end_ts = None, "idle", None
                if a.finish_task:
                    a.finish_task.cancel()
                await self.broadcast(a.name, "match.uncalled", {"match": m, "timer": None})
            return "uncalled"
        if cmd in ("start", "pause", "reset", "adjust"):
            if a.match_id is None:
                return "no match called"
            if cmd == "start" and a.state in ("idle", "paused"):
                a.end_ts = time.time() + a.remaining
                a.state = "running"
                evt = "timer.started"
            elif cmd == "pause" and a.state == "running":
                a.remaining = a.remaining_now()
                a.state, a.end_ts = "paused", None
                evt = "timer.paused"
            elif cmd == "reset":
                a.state, a.remaining, a.end_ts = "idle", float(a.duration), None
                evt = "timer.reset"
            elif cmd == "adjust" and a.state != "running":
                a.remaining = float(int(value))
                evt = "timer.adjusted"
            else:
                return f"{cmd} not allowed in state {a.state}"
            a.version += 1
            a.last_source = {"type": "arena_manager"}
            await self.after_change(a, evt)
            return evt
        if cmd == "duration":
            if a.state != "idle":
                return "duration only changeable while idle"
            a.duration = int(value)
            a.remaining = float(a.duration)
            a.version += 1
            a.last_source = {"type": "event_manager"}
            await self.after_change(a, "timer.adjusted")
            return "duration set"
        if cmd == "authority":
            if value in ("rce", "rce_manager", ""):
                a.authority = {"type": "rce_manager"}
            elif value in self.devices_seen:
                a.authority = {"type": "device", "device": value}
            else:
                return f"unknown device {value!r}; seen: {sorted(self.devices_seen)}"
            await self.broadcast(a.name, "queue.updated", {"arena": a.arena_obj()})
            return f"authority={a.authority}"
        if cmd == "skew":
            # Test hook: silently move the server's running clock by N seconds
            # (no broadcast), as if the server and the device had drifted apart.
            if a.state != "running":
                return "skew only applies while running"
            a.end_ts += float(value)
            return f"server clock skewed by {value}s; remaining now {a.remaining_now():.1f}"
        if cmd == "kick":
            for c in list(self.conns):
                c.open = False
                c.writer.close()
            return "kicked"
        if cmd == "refuse":
            self.refuse = value in ("1", "true", "on")
            return f"refuse={self.refuse}"
        if cmd == "silence":
            self.silence = value in ("1", "true", "on")
            return f"silence={self.silence}"
        return "unknown cmd"

    async def handle_admin(self, reader, writer):
        try:
            raw = await reader.readuntil(b"\r\n\r\n")
            target = raw.split(b" ")[1].decode()
            u = urllib.parse.urlparse(target)
            q = {k: v[0] for k, v in urllib.parse.parse_qs(u.query).items()}
            if u.path == "/admin/state":
                body = json.dumps({
                    "arenas": {n: a.arena_obj() for n, a in self.arenas.items()},
                    "devices_seen": sorted(self.devices_seen),
                    "connected": [{"peer": c.peer, "device": c.device, "arenas": c.arenas} for c in self.conns],
                    "seq": self.seq, "log": self.log[-120:]})
            else:
                res = await self.admin(q.get("cmd", ""), q.get("value", ""), q.get("arena", ""))
                self.note("AD", "admin", f"{q} -> {res}")
                body = json.dumps({"result": res})
            writer.write(b"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n"
                         b"Content-Length: %d\r\n\r\n%s" % (len(body), body.encode()))
            await writer.drain()
        except Exception as e:
            print("admin error", e, file=sys.stderr)
        finally:
            writer.close()


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8765, help="WebSocket port (ws://host:port/cable)")
    ap.add_argument("--admin-port", type=int, default=8766)
    ap.add_argument("--key", default="rcm_test_mockkey0000000000000000")
    ap.add_argument("--arenas", default="Arena 1,Arena 2")
    args = ap.parse_args()
    s = Server(args.key, [x.strip() for x in args.arenas.split(",")])
    await asyncio.start_server(s.handle_ws, args.host, args.port)
    await asyncio.start_server(s.handle_admin, args.host, args.admin_port)
    asyncio.create_task(s.ping_loop())
    print(f"mock RCE Manager: ws://{args.host}:{args.port}/cable  admin http://{args.host}:{args.admin_port}/admin"
          f"  key={args.key}  arenas={args.arenas}", flush=True)
    await asyncio.Event().wait()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
