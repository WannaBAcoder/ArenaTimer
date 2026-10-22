# RCE Manager mock

A stand-in for [RCE Manager](https://www.rcemanager.com)'s hardware-timer interface, written from the docs in [BotCodeBashers/rce-manager-integrations](https://github.com/BotCodeBashers/rce-manager-integrations) (`live-v1.md`, `timer-control-v1.md`). It lets the ArenaTimer's RCE integration be developed and tested without access to the real service or to any RCE Manager hardware.

It is a test double, **not** the real service. Where the docs are silent, the choice made here is marked `UNDOCUMENTED` in the source. Standard library only: nothing to install (Python 3.9+).

## Run

```bash
python mock_rce_server.py                 # ws://0.0.0.0:8765/cable, admin on http://0.0.0.0:8766
python selftest.py                        # exercises it like a device would: 18 checks
```

Options: `--port`, `--admin-port`, `--key`, `--arenas "Arena 1,Arena 2"`. Defaults: key `rcm_test_mockkey0000000000000000`, arenas `Arena 1` and `Arena 2`.

## Connect a timer

On the timer's web page, RCE Manager box:

| Field | Value |
|---|---|
| Arena name | `Arena 1` |
| Integration key | `rcm_test_mockkey0000000000000000` |
| Advanced → Host | this PC's LAN IP (the timer must be on the same network) |
| Advanced → Port | `8765` |
| Advanced → TLS | **off** (the mock is plain `ws://`) |

The server logs every connection and frame to stdout. A TLS attempt is detected and reported, since that is the usual reason a timer never connects.

## What it implements

* WebSocket handshake with `Authorization: Bearer` (or `?key=`), `welcome`, `subscribe` → `snapshot` → `confirm_subscription`, `reject_subscription`, `disconnect`/`unauthorized`, `ping` every 3 s
* `seq`-numbered change messages (`timer.started/paused/reset/adjusted/finished`, `match.called/uncalled`, `queue.updated`)
* Device commands `start`, `pause`, `reset`, `adjust` with `ack`/`error`, idempotency `id`s, `stale_version`, `no_match`, `already_running`, `not_running`, `timer_running`, `match_complete`, and clock **authority** (a device's `remaining_s` is honored only when it runs the clock)
* Per-match timer `version`, the server finishing a running timer at zero

## Admin API: play the Arena Manager's tablet

`GET http://localhost:8766/admin?cmd=<cmd>[&value=<v>][&arena=<name>]`

| cmd | Effect |
|---|---|
| `call` / `uncall` | Call a match to the arena / remove it |
| `start` / `pause` / `reset` | Tablet buttons |
| `adjust&value=90` | Set remaining seconds (not while running) |
| `duration&value=180` | Change match length (idle only) |
| `authority&value=<device>` | Organizer assigns the clock to a device that has connected (`value=rce` hands it back) |
| `kick` | Close every device connection |
| `refuse&value=1\|0` | Reject new connections (with `kick`, simulates an outage; state is kept) |
| `silence&value=1\|0` | Stop / resume ping frames (exercises the 10 s watchdog) |
| `skew&value=5` | Silently shift the running clock (provoke drift) |

`GET /admin/state` returns JSON: arena timers, devices seen, connected devices, and the recent frame log.

## Test recipes

With a timer connected, drive it and watch its own status (`GET http://<timer-ip>/status`, see the `state`, `currentTime` and `rce` fields):

```bash
curl "localhost:8766/admin?cmd=call"                  # timer shows the match length, idle
curl "localhost:8766/admin?cmd=start"                 # timer counts down
curl "localhost:8766/admin?cmd=pause"                 # timer freezes
curl "localhost:8766/admin?cmd=reset"                 # back to idle
```

* **Controls reach the server:** press Start/Pause/Reset on the timer (or a Judge remote) and watch `GET /admin/state` (`last_source` = the device name).
* **Mid-match outage:** `start`, then `kick`. The timer should reconnect within a couple of seconds, keep its clock, and follow a following `pause`.
* **Silent connection:** `silence&value=1` for 15 s; the timer should reconnect after about 10 s.
* **Started offline:** `refuse&value=1`, `kick`, press Start on the timer, `refuse&value=0`. The timer must keep running and the server should end up running with the timer's time.
* **Authority:** `authority&value=<device name>`, press Start on the timer, then let it run to zero: the server should end with the match `finished`.
