"""Preview the dashboard on a laptop, no ESP32 needed.

    python tools/preview_server.py              # opens http://localhost:8000
    python tools/preview_server.py --present    # class presentation timeline
    python tools/preview_server.py --speed 10   # 10x faster: LATE/LOST sooner

Serves web/index.html (re-read on every request, so edit and refresh) and a
fake /data endpoint with the same scenarios as WebDashboardDemo.h:

Realistic (default, = DASH_DEMO 1), repeats every 10 minutes:
  - 3 rescuers walking near Snow Canyon, Utah, one packet each ~30 s +/- 4 s
  - about 1 in 12 packets lost on air (shows as GAP)
  - Node 2's packet at minute 2:00 arrives twice (DUPLICATE)
  - Node 3 out of range from 4:00 to 7:00: LATE after 60 s, LOST after 150 s,
    then it recovers with a GAP

Presentation (--present, = DASH_DEMO 2), 6x faster and scripted, repeats
every 2:30 (times from server start):
  0:35 Node 1 GAP, 0:55 Node 2 duplicate, 1:10 Node 3 out of range
  (LATE ~1:20, LOST + alert ~1:35), 1:55 Node 3 back with a GAP.
  LATE after 10 s, LOST after 25 s. See DEMO.md.

Standard library only. With --speed above 1 the chart and log times look
squeezed together (the fake clock runs faster than the browser's); without
--speed, times are real.
"""

import argparse
import json
import math
import random
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

PAGE = Path(__file__).resolve().parent.parent / "web" / "index.html"

# Same values as WebDashboard.h
MAX_NODES = 3
HISTORY = 64
OLD_WINDOW = 64
MAX_GAP = 1000
CLOCK_SLACK_MS = 1000
BASE_LAT, BASE_LNG = 37.2000, -113.6400
NEVER = 0xFFFFFFFF

# Same values as DEMO_REALISTIC / DEMO_PRESENTATION in WebDashboardDemo.h
REALISTIC = dict(send_ms=30_000, jitter_ms=4_000, drop_one_in=12, cycle_ms=600_000,
                 silent_from_ms=240_000, silent_to_ms=420_000, gap_at_ms=NEVER,
                 dup_at_ms=120_000, step_min_m=15, step_max_m=40,
                 late_ms=60_000, lost_ms=150_000, mode="Demo data")
PRESENTATION = dict(send_ms=5_000, jitter_ms=800, drop_one_in=0, cycle_ms=150_000,
                    silent_from_ms=70_000, silent_to_ms=115_000, gap_at_ms=35_000,
                    dup_at_ms=55_000, step_min_m=6, step_max_m=14,
                    late_ms=10_000, lost_ms=25_000, mode="Presentation demo")


class Dashboard:
    """Python copy of the WebDashboard update and JSON logic."""

    def __init__(self, late_ms, lost_ms, mode):
        self.late_ms, self.lost_ms, self.mode = late_ms, lost_ms, mode
        self.boot_id = random.randint(1, 0x7FFFFFFF)  # new on every restart
        self.nodes = {n: self._empty() for n in range(1, MAX_NODES + 1)}
        self.history = []
        self.next_uid = 1
        self.lock = threading.Lock()

    @staticmethod
    def _empty():
        return {"last": None, "rx": 0, "received": 0, "missed": 0,
                "duplicates": 0, "old": 0, "reboots": 0}

    def update(self, r, now):
        with self.lock:
            if not 1 <= r["node"] <= MAX_NODES:
                return "BAD_NODE"
            st = self.nodes[r["node"]]
            last = st["last"]
            if last is None:
                return self._accept(st, r, "NEW", now)
            fwd = (r["seq"] - last["seq"]) & 0xFFFF
            back = (last["seq"] - r["seq"]) & 0xFFFF
            clock_back = r["elapsedMs"] + CLOCK_SLACK_MS < last["elapsedMs"]
            if fwd == 0 and not clock_back:
                st["duplicates"] += 1
                return "DUPLICATE"
            if fwd != 0 and back <= OLD_WINDOW:
                st["old"] += 1
                return "OLD"
            if fwd == 0 or clock_back or fwd - 1 > MAX_GAP:
                st["reboots"] += 1
                return self._accept(st, r, "NEW", now)
            if fwd > 1:
                st["missed"] += fwd - 1
                return self._accept(st, r, "GAP", now)
            return self._accept(st, r, "NEW", now)

    def _accept(self, st, r, result, now):
        st.update(last=r, rx=now)
        st["received"] += 1
        self.history.append({"uid": self.next_uid, "result": result, "rx": now, "r": r})
        self.history = self.history[-HISTORY:]
        self.next_uid += 1
        return result

    def status(self, st, now):
        if st["last"] is None:
            return "NO_DATA"
        age = now - st["rx"]
        return "LOST" if age >= self.lost_ms else "LATE" if age >= self.late_ms else "OK"

    @staticmethod
    def _fields(r):
        return {"lat": round(r["lat"], 6), "lng": round(r["lng"], 6),
                "temp": round(r["temp"], 1), "humidity": round(r["humidity"], 1),
                "batteryMv": r["batteryMv"], "rssi": r["rssi"], "hopLimit": r["hops"]}

    def to_json(self, now, since):
        with self.lock:
            nodes = []
            for n, st in self.nodes.items():
                item = {"id": n, "status": self.status(st, now)}
                if st["last"] is not None:
                    r = st["last"]
                    item.update(ageMs=now - st["rx"], seq=r["seq"], elapsedMs=r["elapsedMs"])
                    item.update(self._fields(r))
                for k in ("received", "missed", "duplicates", "old", "reboots"):
                    item[k] = st[k]
                nodes.append(item)
            packets = []
            for p in self.history:
                if p["uid"] > since:
                    r = p["r"]
                    packets.append({"uid": p["uid"], "node": r["node"], "seq": r["seq"],
                                    "result": p["result"], "receivedAtMs": p["rx"],
                                    "elapsedMs": r["elapsedMs"], **self._fields(r)})
            out = {"nowMs": now, "uptimeMs": now, "bootId": self.boot_id,
                   "lateMs": self.late_ms, "lostMs": self.lost_ms}
            if self.mode:
                out["mode"] = self.mode
            out.update(nodes=nodes, packets=packets, lastId=self.next_uid - 1, more=False)
            return out


class FakeNode:
    """One rescuer walking around with a sensor node."""

    START = {1: (37.2012, -113.6431, 45), 2: (37.1987, -113.6385, 0),
             3: (37.2030, -113.6370, 270)}
    TEMP = {1: 24.0, 2: 27.0, 3: 22.0}
    HUM = {1: 30.0, 2: 24.0, 3: 36.0}
    BATT = {1: 4150, 2: 4050, 3: 3900}

    def __init__(self, node, sc):
        self.node, self.sc = node, sc
        self.lat, self.lng, self.heading = self.START[node]
        self.seq = 0
        self.boot_offset_ms = node * 45_000  # node booted before the base did
        self.next_ms = 2_000 + (sc["send_ms"] // 10) * (node - 1)
        self.batt = self.BATT[node]

    def walk(self):
        self.heading = (self.heading + random.uniform(-35, 35)) % 360
        dist = random.uniform(self.sc["step_min_m"], self.sc["step_max_m"])
        rad = math.radians(self.heading)
        self.lat += dist * math.cos(rad) / 110_540
        self.lng += dist * math.sin(rad) / (111_320 * math.cos(math.radians(self.lat)))

    def reading(self, t):
        dx = (self.lng - BASE_LNG) * 111_320 * math.cos(math.radians(BASE_LAT))
        dy = (self.lat - BASE_LAT) * 110_540
        dist = math.hypot(dx, dy)
        minutes = t / 60_000 * (30_000 / self.sc["send_ms"])  # "field time"
        self.batt = max(3300, self.batt - random.choice((0, 1, 2)))
        return {
            "node": self.node, "seq": self.seq, "elapsedMs": t + self.boot_offset_ms,
            "lat": self.lat, "lng": self.lng,
            "temp": self.TEMP[self.node] + 1.5 * math.sin(minutes / 7 + self.node) + random.uniform(-0.3, 0.3),
            "humidity": max(0.0, min(100.0, self.HUM[self.node] + 3 * math.cos(minutes / 9 + self.node) + random.uniform(-1, 1))),
            "hops": 2 if (self.node == 3 and random.random() < 0.4) else 3,
            "batteryMv": self.batt + random.randint(-4, 4),
            "rssi": max(-125, int(-68 - dist / 25 + random.uniform(-4, 4))),
        }


class Simulation:
    def __init__(self, sc, speed):
        self.sc, self.speed = sc, speed
        self.start = time.monotonic()
        self.dash = Dashboard(sc["late_ms"], sc["lost_ms"], sc["mode"])
        self.nodes = [FakeNode(n, sc) for n in range(1, MAX_NODES + 1)]
        self.gap_cycle = self.dup_cycle = None
        self.dup = None  # (due_ms, reading)
        self.lock = threading.Lock()

    def now_ms(self):
        return int((time.monotonic() - self.start) * 1000 * self.speed)

    def _run(self, now):
        """Send every packet that is due by `now` (mirrors DashboardDemo::run)."""
        sc = self.sc
        for n in self.nodes:
            while n.next_ms <= now:
                t = n.next_ms
                cycle, in_cycle = divmod(t, sc["cycle_ms"])
                n.next_ms += sc["send_ms"] + random.randint(-sc["jitter_ms"], sc["jitter_ms"])
                n.walk()
                n.seq = (n.seq + 1) & 0xFFFF
                if n.node == 3 and sc["silent_from_ms"] <= in_cycle < sc["silent_to_ms"]:
                    continue  # out of range: sent but never heard (GAP later)
                if n.node == 1 and in_cycle >= sc["gap_at_ms"] and self.gap_cycle != cycle:
                    self.gap_cycle = cycle
                    continue  # scripted loss
                if sc["drop_one_in"] and random.randrange(sc["drop_one_in"]) == 0:
                    continue  # random loss on air
                r = n.reading(t)
                self.dash.update(r, now)
                if n.node == 2 and in_cycle >= sc["dup_at_ms"] and self.dup_cycle != cycle:
                    self.dup_cycle = cycle
                    self.dup = (now + 1_500, dict(r))  # heard again via a relay
        if self.dup and self.dup[0] <= now:
            self.dash.update(self.dup[1], now)
            self.dup = None

    def data(self, since):
        with self.lock:
            now = self.now_ms()
            self._run(now)
            return self.dash.to_json(now, since)


def make_handler(sim):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            url = urlparse(self.path)
            if url.path in ("/", "/index.html"):
                self._send(200, "text/html; charset=utf-8", PAGE.read_bytes())
            elif url.path == "/data":
                try:
                    since = int(parse_qs(url.query).get("since", ["0"])[0])
                except ValueError:
                    since = 0
                body = json.dumps(sim.data(since), separators=(",", ":")).encode()
                self._send(200, "application/json", body)
            else:
                self._send(404, "text/plain", b"Not found")

        def _send(self, code, ctype, body):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *args):
            pass  # keep the terminal quiet

    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--present", action="store_true",
                        help="class presentation timeline (6x faster, scripted)")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="run the fake clock faster, e.g. 10")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    sc = PRESENTATION if args.present else REALISTIC
    sim = Simulation(sc, args.speed)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(sim))
    url = f"http://localhost:{args.port}"
    print(f"Dashboard preview at {url}  ({sc['mode']}, speed x{args.speed:g}, Ctrl+C to stop)")
    if args.present:
        print("Timeline: 0:35 GAP | 0:55 duplicate | ~1:20 Node 3 LATE | ~1:35 LOST | "
              "1:55 back | repeats every 2:30")
    if not args.no_browser:
        threading.Timer(0.5, webbrowser.open, args=(url,)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
