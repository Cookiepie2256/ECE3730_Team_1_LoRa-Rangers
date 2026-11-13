"""Tests for the laptop CSV and LIVE modes in tools/preview_server.py
(standard library; one extra check runs if pyserial is installed).

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


HEADER = "pc_time,node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,battery_mv,rssi_dbm"


def csv_row(stamp, node, seq, elapsed=1000, temp="25.4"):
    return f"{stamp},{node},{seq},{elapsed},37.104000,-113.584000,{temp},36.5,3,3850,-87\r\n"


def test_csv_feed():
    """The CSV written by tools/serial_to_csv.py (`just log`) drives the page."""
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "packets.csv"
        feed = ps.CsvFeed(path)
        check(feed.poll() == 0 and feed.data(0)["lastId"] == 0, "missing file: no data, no crash")

        with path.open("w", newline="", encoding="utf-8") as f:
            f.write(HEADER + "\r\n")
            f.write(csv_row("2026-11-13T10:00:00", 1, 1, 1000))
            f.write(csv_row("2026-11-13T10:00:10", 2, 5, 2000))
            f.write(csv_row("2026-11-13T10:00:30", 1, 2, 31000))
            f.write(csv_row("2026-11-13T10:00:40", 2, 8, 32000))    # GAP: 6, 7 missed
            f.write(csv_row("2026-11-13T10:00:41", 2, 8, 32000))    # DUPLICATE
            f.write(csv_row("2026-11-13T10:00:42", 0, 1))           # node 0: BAD_NODE
            f.write("2026-11-13T10:00:43,1,2,3\r\n")                # wrong field count
            f.write(csv_row("not-a-time", 1, 3))                    # bad pc_time
            f.write(csv_row("2026-11-13T10:01:00", 3, 1, 500, "nan"))
            f.write("2026-11-13T10:01:30,1,3,61000,37.1")           # half-written row
        check(feed.poll() == 6, "6 valid rows read (header, bad and partial rows left out)")
        d = json.loads(json.dumps(feed.data(0), allow_nan=False))
        check(d["source"] == "csv" and d["mode"] == "CSV: packets.csv", "source and badge")
        check(d["lateMs"] == 60000 and d["lostMs"] == 150000, "real thresholds")
        pk = d["packets"]
        check([(p["node"], p["seq"], p["result"]) for p in pk]
              == [(1, 1, "NEW"), (2, 5, "NEW"), (1, 2, "NEW"), (2, 8, "GAP"), (3, 1, "NEW")],
              "packets and results in file order")
        check([p["receivedAtMs"] for p in pk] == [0, 10000, 30000, 40000, 60000],
              "packet times follow pc_time")
        n1, n2, n3 = d["nodes"]
        check(n2["missed"] == 2 and n2["duplicates"] == 1 and n2["received"] == 2, "node 2 counters")
        check(n1["received"] == 2 and n1["seq"] == 2 and n1["batteryMv"] == 3850
              and n1["rssi"] == -87 and n1["hopLimit"] == 3 and n1["lat"] == 37.104
              and n1["lng"] == -113.584 and n1["temp"] == 25.4 and n1["humidity"] == 36.5,
              "node 1 shows the CSV values")
        check(n3["temp"] is None, "nan temperature sent as null")
        check(n1["status"] == "LOST" and n1["ageMs"] == d["nowMs"] - 30000,
              "age and status come from the recorded time")
        check(feed.skipped == 3, "3 bad rows skipped")

        # `just log` finishes the half-written row and appends another
        with path.open("a", newline="", encoding="utf-8") as f:
            f.write("04000,-113.584000,25.5,36.0,3,3849,-88\r\n")
            f.write(csv_row("2026-11-13T10:02:00", 1, 4, 91000))
        check(feed.poll() == 2, "appended rows picked up, split row read once complete")
        d = feed.data(5)
        check([(p["uid"], p["seq"]) for p in d["packets"]] == [(6, 3), (7, 4)],
              "only the new rows are sent after `since`")
        check(feed.poll() == 0, "nothing new: nothing read")
        check(path.read_text(encoding="utf-8").count("\n") == 12, "the CSV itself is never modified")

        # A new recording replaces the file: start over with a new boot ID
        boot = feed.dash.boot_id
        with path.open("w", newline="", encoding="utf-8") as f:
            f.write(HEADER + "\r\n" + csv_row("2026-11-14T09:00:00", 3, 1))
        check(feed.poll() == 1, "replaced file reloaded")
        d = feed.data(0)
        check(d["lastId"] == 1 and d["packets"][0]["node"] == 3 and d["nodes"][0]["received"] == 0,
              "old rows are gone after the file is replaced")
        check(feed.dash.boot_id != boot, "new boot ID tells the page to start over")

        # A long recording is sent in pages of PAGE_SIZE
        big = Path(tmp) / "big.csv"
        with big.open("w", newline="", encoding="utf-8") as f:
            f.write(HEADER + "\r\n")
            for i in range(1200):
                f.write(csv_row(f"2026-11-13T{10 + i // 3600:02d}:{i // 60 % 60:02d}:{i % 60:02d}",
                                i % 3 + 1, i // 3 + 1, i * 1000))
        feed = ps.CsvFeed(big)
        check(feed.poll() == 1200, "1200 rows loaded")
        since, uids, pages = 0, [], 0
        while True:
            d = feed.data(since)
            uids += [p["uid"] for p in d["packets"]]
            since, pages = d["lastId"], pages + 1
            if not d["more"]:
                break
        check(pages == 3 and uids == list(range(1, 1201)), "3 pages, every packet exactly once")
        check(all(n["missed"] == 0 and n["received"] == 400 for n in d["nodes"]), "no false gaps")

    try:  # The header must stay the one Braxton's logger writes
        sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
        import serial_to_csv
        check(",".join(serial_to_csv.HEADER) == HEADER
              and serial_to_csv.FIELDS == ps.CSV_FIELDS, "same columns as tools/serial_to_csv.py")
    except ImportError:
        print("  (skipped the tools/serial_to_csv.py column check: pyserial not installed)")


def test_sources():
    check(ps.Simulation(ps.REALISTIC, 1.0).data(0)["source"] == "demo", "demo data is labelled")
    check(ps.LiveFeed().data(0)["source"] == "serial", "serial data is labelled")


def test_demo_json_valid():
    sim = ps.Simulation(ps.PRESENTATION, speed=1.0)
    d = sim.dash.to_json(0, 0)
    json.dumps(d, allow_nan=False)
    check(d["mode"] == "Presentation demo", "presentation badge")


if __name__ == "__main__":
    for t in (test_parse, test_feed_and_json, test_mac_ids, test_csv_feed, test_sources,
              test_demo_json_valid):
        before = failures
        t()
        print(f"{'[PASS]' if failures == before else '[FAIL]'} {t.__name__}")
    print("live feed tests:", "all passed" if failures == 0 else f"{failures} failed")
    sys.exit(1 if failures else 0)
