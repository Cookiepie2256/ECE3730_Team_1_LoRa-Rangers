"""Run the dashboard on a laptop from the base station's CSV log.

    python tools/preview_server.py                    # REAL: follows packets.csv
    python tools/preview_server.py --csv run1.csv     # REAL: another CSV file
    python tools/preview_server.py --serial COM3 [--log packets.csv]   # REAL
    python tools/preview_server.py --stdin            # REAL: typed/pasted lines
    python tools/preview_server.py --demo             # FAKE data (opt-in)
    python tools/preview_server.py --present          # FAKE: class presentation
    python tools/preview_server.py --demo --speed 10  # FAKE, 10x faster

CSV mode (the default) is the team's data path:

    [nodes] --LoRa--> [base ESP32] --USB--> `just log <port>` --> packets.csv
                                                                      |
                                                       this server <--+

Braxton's tools/serial_to_csv.py owns the serial port and appends one row per
packet: pc_time,node_id,seq,elapsed_ms,latitude,longitude,temp_c,
humidity_pct,hops,battery_mv,rssi_dbm. This server loads that file and keeps
following it, so the page shows every recorded packet at the time the PC
logged it, and new packets about half a second after they are written. It
never writes to the CSV. Start the two programs in either order.

--serial reads the base station's USB serial output directly instead. Only
one program can open a serial port, so it cannot run next to `just log`
(use --log to keep a CSV in the same format). --serial needs pyserial;
everything else is standard library only.

Fake data is never shown unless --demo or --present is given, and the page
drops its stored history whenever the data source changes, so fake packets
cannot end up next to real ones.

Serves web/index.html (re-read on every request, so edit and refresh). The
fake /data endpoint uses the same scenarios as WebDashboardDemo.h:

Realistic (--demo, = DASH_DEMO 1), repeats every 10 minutes:
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
import sys
import threading
import time
import webbrowser
from datetime import datetime
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
PAGE_SIZE = 500      # most packets in one /data response ("more" pages the rest)
CSV_HISTORY = 5000   # packets kept in CSV mode (same as CAP in web/index.html)

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

    def __init__(self, late_ms, lost_ms, mode, source=None, history_limit=HISTORY):
        self.late_ms, self.lost_ms, self.mode = late_ms, lost_ms, mode
        self.source = source  # "csv", "demo" or "serial": tells the page what it shows
        self.history_limit = history_limit
        self.boot_id = random.randint(1, 0x7FFFFFFF)  # new on every restart
        self.nodes = {n: self._empty() for n in range(1, MAX_NODES + 1)}  # by slot
        self.history = []
        self.next_uid = 1
        self.lock = threading.Lock()

    @staticmethod
    def _empty():
        return {"id": 0, "last": None, "rx": 0, "received": 0, "missed": 0,
                "duplicates": 0, "old": 0, "reboots": 0}

    def update(self, r, now):
        with self.lock:
            if r["node"] == 0:
                return "BAD_NODE"
            slot = self._slot_of(r["node"])
            if slot == 0:
                slot = self._claim(r["node"])
                if slot == 0:
                    return "BAD_NODE"  # every slot belongs to another node
                return self._accept(slot, r, "NEW", now)
            st = self.nodes[slot]
            last = st["last"]
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
                return self._accept(slot, r, "NEW", now)
            if fwd > 1:
                st["missed"] += fwd - 1
                return self._accept(slot, r, "GAP", now)
            return self._accept(slot, r, "NEW", now)

    def _slot_of(self, node_id):
        return next((s for s, st in self.nodes.items() if st["id"] == node_id), 0)

    def _claim(self, node_id):
        # Same rule as WebDashboard::claimSlot: ID N keeps slot N if it is free
        if node_id <= MAX_NODES and self.nodes[node_id]["id"] == 0:
            self.nodes[node_id]["id"] = node_id
            return node_id
        for s, st in self.nodes.items():
            if st["id"] == 0:
                st["id"] = node_id
                return s
        return 0

    def _accept(self, slot, r, result, now):
        st = self.nodes[slot]
        st.update(last=r, rx=now)
        st["received"] += 1
        self.history.append({"uid": self.next_uid, "result": result, "rx": now,
                             "slot": slot, "r": r})
        if len(self.history) > self.history_limit:
            del self.history[0]
        self.next_uid += 1
        return result

    def status(self, st, now):
        if st["last"] is None:
            return "NO_DATA"
        age = now - st["rx"]
        return "LOST" if age >= self.lost_ms else "LATE" if age >= self.late_ms else "OK"

    @staticmethod
    def _fields(r):
        def num(v, digits):  # JSON has no NaN: a failed sensor becomes null
            return round(v, digits) if math.isfinite(v) else None
        return {"lat": num(r["lat"], 6), "lng": num(r["lng"], 6),
                "temp": num(r["temp"], 1), "humidity": num(r["humidity"], 1),
                "batteryMv": r["batteryMv"], "rssi": r["rssi"], "hopLimit": r["hops"]}

    def to_json(self, now, since, uptime=None):
        with self.lock:
            nodes = []
            for n, st in self.nodes.items():
                item = {"id": n, "status": self.status(st, now)}
                if st["id"]:
                    item["nodeId"] = st["id"]
                if st["last"] is not None:
                    r = st["last"]
                    item.update(ageMs=now - st["rx"], seq=r["seq"], elapsedMs=r["elapsedMs"])
                    item.update(self._fields(r))
                for k in ("received", "missed", "duplicates", "old", "reboots"):
                    item[k] = st[k]
                nodes.append(item)
            packets = []
            last_id, more = self.next_uid - 1, False
            for p in self.history:
                if p["uid"] > since:
                    if len(packets) == PAGE_SIZE:  # the page asks again from lastId
                        last_id, more = packets[-1]["uid"], True
                        break
                    r = p["r"]
                    packets.append({"uid": p["uid"], "node": p["slot"], "nodeId": r["node"],
                                    "seq": r["seq"],
                                    "result": p["result"], "receivedAtMs": p["rx"],
                                    "elapsedMs": r["elapsedMs"], **self._fields(r)})
            out = {"nowMs": now, "uptimeMs": now if uptime is None else uptime,
                   "bootId": self.boot_id, "lateMs": self.late_ms, "lostMs": self.lost_ms}
            if self.mode:
                out["mode"] = self.mode
            if self.source:
                out["source"] = self.source
            out.update(nodes=nodes, packets=packets, lastId=last_id, more=more)
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
        self.dash = Dashboard(sc["late_ms"], sc["lost_ms"], sc["mode"], source="demo")
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


PREFIX = "[CSVDATA]"
CSV_FIELDS = ["node_id", "seq", "elapsed_ms", "latitude", "longitude", "temp_c",
              "humidity_pct", "hops", "battery_mv", "rssi_dbm"]
REAL_LATE_MS, REAL_LOST_MS = 60_000, 150_000


def parse_csv(line, require_prefix=False):
    """Parse one SerialForwarder line, with the same rules as
    WebDashboard::updateFromCsv. Returns a reading dict, or None."""
    s = line.strip()
    if s.startswith(PREFIX):
        s = s[len(PREFIX):].strip()
    elif require_prefix:
        return None
    parts = [p.strip() for p in s.split(",")]
    if len(parts) != len(CSV_FIELDS) or any(not p or "_" in p for p in parts):
        return None
    try:
        node, seq, elapsed = int(parts[0]), int(parts[1]), int(parts[2])
        lat, lng, temp, hum = (float(p) for p in parts[3:7])
        hops, batt, rssi = int(parts[7]), int(parts[8]), int(parts[9])
    except ValueError:
        return None

    def real(v, lo, hi, allow_nan=False):
        return (allow_nan and math.isnan(v)) or (math.isfinite(v) and lo <= v <= hi)

    if not (0 <= node <= 65535 and 0 <= seq <= 65535 and -2**31 <= elapsed < 2**31
            and real(lat, -90, 90) and real(lng, -180, 180)
            and real(temp, -100, 150, True) and real(hum, 0, 100, True)
            and 0 <= hops <= 255 and 0 <= batt <= 65535 and -200 <= rssi <= 50):
        return None
    return {"node": node, "seq": seq, "elapsedMs": elapsed, "lat": lat, "lng": lng,
            "temp": temp, "humidity": hum, "hops": hops, "batteryMv": batt, "rssi": rssi}


class LiveFeed:
    """Real packets: lines from the base station's USB serial or typed in."""

    def __init__(self, log_path=None):
        self.start = time.monotonic()
        self.dash = Dashboard(REAL_LATE_MS, REAL_LOST_MS, None, source="serial")
        self.log = None
        if log_path:
            # Same columns as tools/serial_to_csv.py, so the files can be merged
            new = not Path(log_path).exists() or Path(log_path).stat().st_size == 0
            self.log = open(log_path, "a", newline="", encoding="utf-8")
            if new:
                self.log.write(",".join(["pc_time", *CSV_FIELDS]) + "\n")

    def now_ms(self):
        return int((time.monotonic() - self.start) * 1000)

    def handle(self, raw, require_prefix):
        line = raw.rstrip("\r\n")
        r = parse_csv(line, require_prefix)
        if r is None:
            if line.strip().startswith(PREFIX) or (not require_prefix and line.strip()):
                print(f"[skipped: not a valid CSV line] {line}")
            elif line.strip():
                print(line)  # other output from the board (debug prints)
            return
        result = self.dash.update(r, self.now_ms())
        print(f"[{result}] node {r['node']} seq {r['seq']}")
        if self.log and result != "BAD_NODE":
            values = line.strip()
            if values.startswith(PREFIX):
                values = values[len(PREFIX):].strip()
            stamp = time.strftime("%Y-%m-%dT%H:%M:%S")
            self.log.write(f"{stamp},{values.replace(' ', '')}\n")
            self.log.flush()

    def data(self, since):
        return self.dash.to_json(self.now_ms(), since)


class CsvFeed:
    """Real packets from the CSV that tools/serial_to_csv.py (`just log`) writes.

    Loads every row already in the file, then keeps following it. Each row is
    stamped with its own pc_time, so history shows when a packet was really
    logged and LATE/LOST come from its real age. The file is only ever read.
    """

    HEAD = 64  # leading bytes remembered to notice that the file was replaced

    def __init__(self, path):
        self.path = Path(path)
        self.started = time.time()
        self.lock = threading.Lock()
        self.waiting = False
        self._reset()

    def _reset(self):
        self.dash = Dashboard(REAL_LATE_MS, REAL_LOST_MS, f"CSV: {self.path.name}",
                              source="csv", history_limit=CSV_HISTORY)
        self.offset = 0      # bytes of the file already consumed
        self.head = b""      # first bytes of what was consumed
        self.t0 = None       # pc_time of the first row, in ms: the dashboard's zero
        self.last_rx = 0
        self.rows = self.skipped = 0

    @staticmethod
    def _stamp_ms(text):
        """pc_time (ISO 8601, as written by serial_to_csv.py) -> epoch ms."""
        try:
            return int(datetime.fromisoformat(text.strip()).timestamp() * 1000)
        except (ValueError, OverflowError, OSError):
            return None

    def _row(self, line):
        line = line.strip()
        if not line or line.startswith("pc_time"):
            return None  # blank line or the header row
        stamp, _, rest = line.partition(",")
        rx = self._stamp_ms(stamp)
        r = parse_csv(rest) if rx is not None else None
        if r is None:
            self.skipped += 1
            print(f"[skipped: not a valid CSV row] {line}")
            return None
        if self.t0 is None:
            self.t0 = rx
        # Never step backwards (PC clock changed mid-run): keeps ages >= 0
        self.last_rx = max(self.last_rx, rx - self.t0)
        result = self.dash.update(r, self.last_rx)
        if result == "BAD_NODE":
            self.skipped += 1
            print(f"[skipped: node ID 0, or all {MAX_NODES} slots taken by other nodes] {line}")
            return None
        self.rows += 1
        return result

    def poll(self):
        """Read the complete lines added since the last call. Returns how
        many rows were read (0 if nothing new)."""
        with self.lock:
            try:
                size = self.path.stat().st_size
                with self.path.open("rb") as f:
                    head = f.read(len(self.head))
                    if size < self.offset or head != self.head:
                        print(f"[csv] {self.path} was replaced: reloading")
                        self._reset()
                    f.seek(self.offset)
                    chunk = f.read()
            except OSError:
                if self.offset:
                    print(f"[csv] {self.path} is gone: waiting for it")
                    self._reset()
                elif not self.waiting:
                    print(f"[csv] waiting for {self.path} (start `just log <port>`)")
                self.waiting = True
                return 0
            self.waiting = False
            end = chunk.rfind(b"\n")
            if end < 0:
                return 0  # no complete new line yet
            if len(self.head) < self.HEAD:
                self.head = (self.head + chunk[:end + 1])[:self.HEAD]
            self.offset += end + 1
            before = self.rows
            for raw in chunk[:end].split(b"\n"):
                self._row(raw.decode("utf-8", errors="replace"))
            added = self.rows - before
            if added:
                print(f"[csv] +{added} row(s), {self.rows} total"
                      + (f", {self.skipped} skipped" if self.skipped else ""))
            return added

    def follow(self, interval=0.5):
        while True:
            self.poll()
            time.sleep(interval)

    def data(self, since):
        with self.lock:
            wall = int(time.time() * 1000)
            now = max(self.last_rx, wall - self.t0) if self.t0 is not None else 0
            uptime = wall - int(self.started * 1000)
            return self.dash.to_json(now, since, uptime)


def read_serial(port, baud, feed):
    import serial  # pyserial; checked in main()
    while True:
        try:
            with serial.serial_for_url(port, baudrate=baud, timeout=1) as ser:
                print(f"Reading {port} @ {baud} baud")
                while True:
                    raw = ser.readline()
                    if raw:
                        feed.handle(raw.decode("utf-8", errors="replace"), require_prefix=True)
        except serial.SerialException as e:
            print(f"[serial error: {e}; retrying in 2 s]")
            time.sleep(2)


def read_stdin(feed):
    print("Type or paste CSV lines (node_id,seq,elapsed_ms,...). Ctrl+C to stop.")
    for line in sys.stdin:
        feed.handle(line, require_prefix=False)


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
    parser.add_argument("--csv", metavar="FILE",
                        help="REAL: follow the CSV written by `just log` "
                             "(this is the default, with packets.csv)")
    parser.add_argument("--serial", metavar="PORT",
                        help="REAL: read [CSVDATA] lines from the base station, e.g. COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--stdin", action="store_true",
                        help="REAL: read CSV lines typed or piped into this terminal")
    parser.add_argument("--log", metavar="FILE",
                        help="with --serial/--stdin: also append each packet to a CSV file")
    parser.add_argument("--demo", action="store_true",
                        help="FAKE data: 3 simulated rescuers (never the default)")
    parser.add_argument("--present", action="store_true",
                        help="FAKE data: class presentation timeline (6x faster, scripted)")
    parser.add_argument("--speed", type=float, default=1.0,
                        help="with --demo/--present: run the fake clock faster, e.g. 10")
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)  # show packets as they arrive

    demo = args.demo or args.present
    chosen = [name for name, on in (("--csv", args.csv), ("--serial", args.serial),
                                    ("--stdin", args.stdin), ("--demo/--present", demo)) if on]
    if len(chosen) > 1:
        parser.error(f"choose one data source, not {' and '.join(chosen)}")
    if args.speed != 1.0 and not demo:
        parser.error("--speed only applies to --demo/--present")
    if args.log and not (args.serial or args.stdin):
        parser.error("--log only applies to --serial/--stdin (CSV mode never writes)")
    url = f"http://localhost:{args.port}"
    if not chosen or args.csv:
        sim = CsvFeed(args.csv or "packets.csv")
        print(f"Dashboard at {url}  (REAL data from {sim.path.resolve()}, "
              "LATE 60 s, LOST 150 s, Ctrl+C to stop)")
        sim.poll()  # load what is already there before the first request
        threading.Thread(target=sim.follow, daemon=True).start()
    elif args.serial or args.stdin:
        if args.serial:
            try:
                import serial  # noqa: F401
            except ImportError:
                parser.error("--serial needs pyserial: python -m pip install pyserial")
        sim = LiveFeed(args.log)
        reader = (read_serial, (args.serial, args.baud, sim)) if args.serial else (read_stdin, (sim,))
        threading.Thread(target=reader[0], args=reader[1], daemon=True).start()
        print(f"Live dashboard at {url}  (LATE 60 s, LOST 150 s, Ctrl+C to stop)")
    else:
        sc = PRESENTATION if args.present else REALISTIC
        sim = Simulation(sc, args.speed)
        print(f"Dashboard preview at {url}  (FAKE data: {sc['mode']}, speed x{args.speed:g}, "
              "Ctrl+C to stop)")
        if args.present:
            print("Timeline: 0:35 GAP | 0:55 duplicate | ~1:20 Node 3 LATE | ~1:35 LOST | "
                  "1:55 back | repeats every 2:30")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(sim))
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
