"""Preview the dashboard on a laptop, no ESP32 needed.

    python tools/preview_server.py              # opens http://localhost:8000
    python tools/preview_server.py --speed 10   # 10x faster: LATE/LOST sooner

Serves web/index.html (re-read on every request, so edit and refresh) and a
fake /data endpoint with the same 3-node scenario as DASH_DEMO in
WebDashboard.ino:
  - 3 rescuers walking near Snow Canyon, Utah, one packet each ~30 s +/- jitter
  - about 1 in 12 packets lost on air (shows as GAP)
  - Node 2 sends one duplicate every 10 minutes (seq % 20 == 4)
  - Node 3 goes silent from minute 4:00 to 7:00 of every 10 minutes:
    LATE after 60 s, LOST after 150 s, then it recovers with a GAP
Standard library only. With --speed above 1 the chart and log times look
squeezed together (the fake clock runs faster than the browser's); at the
default speed and on the board they are real.
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

# Same values as WebDashboard.h / WebDashboard.ino
MAX_NODES = 3
LATE_MS = 60_000
LOST_MS = 150_000
HISTORY = 64
OLD_WINDOW = 64
MAX_GAP = 1000
CLOCK_SLACK_MS = 1000

SEND_MS = 30_000
JITTER_MS = 4_000
DROP_CHANCE = 1 / 12
CYCLE_MS = 600_000
SILENT_FROM_MS = 240_000
SILENT_TO_MS = 420_000
BASE_LAT, BASE_LNG = 37.2000, -113.6400


class Dashboard:
    """Python copy of the WebDashboard update and JSON logic."""

    def __init__(self):
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
        return "LOST" if age >= LOST_MS else "LATE" if age >= LATE_MS else "OK"

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
            return {"nowMs": now, "uptimeMs": now, "lateMs": LATE_MS, "lostMs": LOST_MS,
                    "nodes": nodes, "packets": packets,
                    "lastId": self.next_uid - 1, "more": False}


class FakeNode:
    """One rescuer walking around with a sensor node."""

    START = {1: (37.2012, -113.6431, 45), 2: (37.1987, -113.6385, 0),
             3: (37.2030, -113.6370, 270)}
    TEMP = {1: 24.0, 2: 27.0, 3: 22.0}
    HUM = {1: 30.0, 2: 24.0, 3: 36.0}
    BATT = {1: 4150, 2: 4050, 3: 3900}

    def __init__(self, node):
        self.node = node
        self.lat, self.lng, self.heading = self.START[node]
        self.seq = 0
        self.boot_offset_ms = node * 45_000  # node booted before the base did
        self.next_ms = 2_000 + 3_000 * (node - 1)
        self.batt = self.BATT[node]

    def silent(self, now):
        return self.node == 3 and SILENT_FROM_MS <= now % CYCLE_MS < SILENT_TO_MS

    def walk(self):
        self.heading = (self.heading + random.uniform(-35, 35)) % 360
        dist = random.uniform(15, 40)  # meters per 30 s
        rad = math.radians(self.heading)
        self.lat += dist * math.cos(rad) / 110_540
        self.lng += dist * math.sin(rad) / (111_320 * math.cos(math.radians(self.lat)))

    def reading(self, now):
        dx = (self.lng - BASE_LNG) * 111_320 * math.cos(math.radians(BASE_LAT))
        dy = (self.lat - BASE_LAT) * 110_540
        dist = math.hypot(dx, dy)
        minutes = now / 60_000
        self.batt = max(3300, self.batt - random.choice((0, 1, 1, 2)))
        return {
            "node": self.node, "seq": self.seq, "elapsedMs": now + self.boot_offset_ms,
            "lat": self.lat, "lng": self.lng,
            "temp": self.TEMP[self.node] + 1.5 * math.sin(minutes / 7 + self.node) + random.uniform(-0.3, 0.3),
            "humidity": max(0.0, min(100.0, self.HUM[self.node] + 3 * math.cos(minutes / 9 + self.node) + random.uniform(-1, 1))),
            "hops": 2 if (self.node == 3 and random.random() < 0.4) else 3,
            "batteryMv": self.batt + random.randint(-4, 4),
            "rssi": max(-125, int(-68 - dist / 25 + random.uniform(-4, 4))),
        }

    def due(self, now, dash):
        """Send every packet that is due by `now`."""
        while self.next_ms <= now:
            t = self.next_ms
            self.next_ms += SEND_MS + random.randint(-JITTER_MS, JITTER_MS)
            self.walk()
            self.seq = (self.seq + 1) & 0xFFFF
            if self.silent(t):
                continue  # out of range: sent but never heard (GAP later)
            if random.random() < DROP_CHANCE:
                continue  # lost on air: base sees a GAP next time
            r = self.reading(t)
            dash.update(r, t)
            if self.node == 2 and self.seq % 20 == 4:
                dash.update(dict(r), t + 1_500)  # same packet relayed twice


class Simulation:
    def __init__(self, speed):
        self.speed = speed
        self.start = time.monotonic()
        self.dash = Dashboard()
        self.nodes = [FakeNode(n) for n in range(1, MAX_NODES + 1)]
        self.lock = threading.Lock()

    def now_ms(self):
        return int((time.monotonic() - self.start) * 1000 * self.speed)

    def data(self, since):
        with self.lock:
            now = self.now_ms()
            for n in self.nodes:
                n.due(now, self.dash)
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
    parser.add_argument("--speed", type=float, default=1.0,
                        help="simulation speed, e.g. 10 = ten times faster")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    sim = Simulation(args.speed)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(sim))
    url = f"http://localhost:{args.port}"
    print(f"Dashboard preview at {url}  (speed x{args.speed:g}, Ctrl+C to stop)")
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
