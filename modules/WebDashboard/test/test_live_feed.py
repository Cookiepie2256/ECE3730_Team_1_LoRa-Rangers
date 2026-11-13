"""Tests for the laptop LIVE mode in tools/preview_server.py (standard library).

    python test/test_live_feed.py
"""

import json
import os
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import preview_server as ps  # noqa: E402

failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print(f"  FAIL {what}")


GOOD = "[CSVDATA] 2,17,512340,37.204512,-113.640231,25.4,31.0,3,3950,-92"

# Same bad lines as testMalformedCsv in test_webdashboard.cpp
BAD = [
    "", "   ", "[CSVDATA] ", "1,2,3", "1,2,3,4,5,6,7,8,9",
    "1,2,3,37.2,-113.6,25,31,3,3950,-92,7",
    "1,2,3,37.2,-113.6,25,31,3,3950,-92,",
    "1,,3,37.2,-113.6,25,31,3,3950,-92",
    "x,2,3,37.2,-113.6,25,31,3,3950,-92",
    "1,2x,3,37.2,-113.6,25,31,3,3950,-92",
    "1,70000,3,37.2,-113.6,25,31,3,3950,-92",
    "1,2,3,91.0,-113.6,25,31,3,3950,-92",
    "1,2,3,37.2,-181.0,25,31,3,3950,-92",
    "1,2,3,nan,-113.6,25,31,3,3950,-92",
    "1,2,3,37.2,-113.6,25,101,3,3950,-92",
    "1,2,3,37.2,-113.6,25,31,256,3950,-92",
    "1,2,3,37.2,-113.6,25,31,3,70000,-92",
    "1,2,3,37.2,-113.6,25,31,3,3950,-300",
    "1,2,99999999999,37.2,-113.6,25,31,3,3950,-92",
    "1;2;3;37.2;-113.6;25;31;3;3950;-92",
    "1,2,3,37.2,-113.6,2_5,31,3,3950,-92",   # Python-only: underscores
    "1,2,3,37.2,-113.6,inf,31,3,3950,-92",
]


def test_parse():
    r = ps.parse_csv(GOOD)
    check(r is not None and r["node"] == 2 and r["seq"] == 17 and r["hops"] == 3
          and r["batteryMv"] == 3950 and r["rssi"] == -92, "valid line parsed")
    check(ps.parse_csv(GOOD[len("[CSVDATA] "):]) is not None, "prefix optional")
    check(ps.parse_csv(GOOD[len("[CSVDATA] "):], require_prefix=True) is None,
          "serial input needs the prefix")
    check(ps.parse_csv("DASH,61000,1,3,GAP,90000,37.2,-113.6,25.4,31.0,3,3950,-92,2,1,0")
          is None, "the board's own log lines are ignored")
    r = ps.parse_csv(" 3 , 1 , 0 , 0 , 0 , nan , nan , 0 , 0 , -120 ")
    check(r is not None and r["temp"] != r["temp"], "nan sensor values allowed")
    for line in BAD:
        check(ps.parse_csv(line) is None, f"rejects {line!r}")


def test_feed_and_json():
    with tempfile.TemporaryDirectory() as tmp:
        log = os.path.join(tmp, "packets.csv")
        feed = ps.LiveFeed(log)
        feed.handle(GOOD + "\r\n", require_prefix=True)
        feed.handle("[CSVDATA] 2,20,602340,37.2046,-113.6403,25.6,30.0,3,3948,-90\n", True)
        feed.handle("[CSVDATA] 2,20,602340,37.2046,-113.6403,25.6,30.0,3,3948,-90\n", True)
        feed.handle("[CSVDATA] 1,1,1000,37.1,-113.5,nan,nan,3,3700,-110\n", True)
        feed.handle("[CSVDATA] 0,1,1000,37.1,-113.5,20,30,3,3700,-110\n", True)
        feed.handle("Booting...\n", True)
        d = feed.data(0)
        text = json.dumps(d, allow_nan=False)  # raises if any NaN slipped through
        back = json.loads(text)
        check(back["lastId"] == 3, "3 packets accepted (dup and node 0 ignored)")
        check([p["result"] for p in back["packets"]] == ["NEW", "GAP", "NEW"], "results")
        n2 = back["nodes"][1]
        check(n2["missed"] == 2 and n2["duplicates"] == 1, "node 2 counters")
        check(back["nodes"][0]["temp"] is None, "nan temperature sent as null")
        check(back["lateMs"] == 60000 and back["lostMs"] == 150000, "real thresholds")
        check("mode" not in back, "no demo badge in live mode")
        feed.log.close()
        rows = Path(log).read_text(encoding="utf-8").splitlines()
        check(rows[0] == "pc_time," + ",".join(ps.CSV_FIELDS), "log header")
        check(len(rows) == 5, "log has the 4 rows for nodes 1..3")
        check(rows[1].endswith(",2,17,512340,37.204512,-113.640231,25.4,31.0,3,3950,-92"),
              "log row keeps the original values")


def test_mac_ids():
    dash = ps.Dashboard(60000, 150000, None)
    r = ps.parse_csv("48001,1,1000,37.1,-113.5,20,30,3,3700,-110")
    check(dash.update(r, 0) == "NEW", "MAC-based ID accepted")
    check(dash.update(dict(r, node=2), 0) == "NEW", "small ID accepted")
    check(dash.update(dict(r, node=1), 0) == "NEW", "ID 1 takes the last slot")
    check(dash.update(dict(r, node=48002), 0) == "BAD_NODE", "no 4th node")
    d = dash.to_json(0, 0)
    check([n.get("nodeId") for n in d["nodes"]] == [48001, 2, 1], "slots match C++")
    check([(p["node"], p["nodeId"]) for p in d["packets"]] == [(1, 48001), (2, 2), (3, 1)],
          "packets carry slot and node ID")


def test_demo_json_valid():
    sim = ps.Simulation(ps.PRESENTATION, speed=1.0)
    d = sim.dash.to_json(0, 0)
    json.dumps(d, allow_nan=False)
    check(d["mode"] == "Presentation demo", "presentation badge")


if __name__ == "__main__":
    for t in (test_parse, test_feed_and_json, test_mac_ids, test_demo_json_valid):
        before = failures
        t()
        print(f"{'[PASS]' if failures == before else '[FAIL]'} {t.__name__}")
    print("live feed tests:", "all passed" if failures == 0 else f"{failures} failed")
    sys.exit(1 if failures else 0)
