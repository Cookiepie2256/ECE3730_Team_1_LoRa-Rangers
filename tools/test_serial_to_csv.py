# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Tests for the base station -> PC -> CSV path.

Two modes:

  Simulated (default, no hardware needed):
      uv run tools/test_serial_to_csv.py
    Feeds known lines through an in-memory serial port (pyserial's loop://)
    into the same code `just log` uses, and checks the CSV it produces.

  Hardware:
      uv run tools/test_serial_to_csv.py --port /dev/ttyUSB0   (Windows: COM3)
    Reads from an ESP32 running modules/SerialForwarder/SerialForwarder.ino.
    That sketch encodes known values with PacketCodec::encode(), decodes them
    with PacketCodec::decode() and sends them with SerialForwarder, so every
    field received here is checked against what the sketch put in.

Exits with status 0 if every check passes, 1 otherwise.
"""

import argparse
import contextlib
import csv
import io
import sys
import tempfile
import time
from datetime import datetime
from pathlib import Path

import serial

from serial_to_csv import FIELDS, HEADER, PREFIX, handle_line, open_csv, parse_data_line

# ----------------------------------------------------------------------------
# Tiny test harness
# ----------------------------------------------------------------------------

results = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    results.append(ok)
    status = "PASS" if ok else "FAIL"
    print(f"  [{status}] {name}" + (f"  ({detail})" if detail and not ok else ""))
    return ok


def section(title: str) -> None:
    print(f"\n== {title} ==")


def read_csv(path: Path):
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.reader(f))


def is_iso_time(text: str) -> bool:
    try:
        datetime.fromisoformat(text)
        return True
    except ValueError:
        return False


# ----------------------------------------------------------------------------
# Expected output of SerialForwarder.ino (keep in sync with that sketch)
# ----------------------------------------------------------------------------


def expected_from_sketch(seq: int) -> dict:
    """Field values SerialForwarder.ino sends for a given sequence number."""
    return {
        "node_id": str(seq % 3 + 1),
        "seq": str(seq),
        "latitude": "37.104000",
        "longitude": "-113.584000",
        "temp_c": "25.4",
        "humidity_pct": "36.5",
        "hops": "3",
        "battery_mv": "3850",
        "rssi_dbm": str(-70 - (seq % 30)),
    }


def sketch_line(seq: int, elapsed_ms: int) -> str:
    """Build the exact line SerialForwarder.ino would print for `seq`."""
    e = expected_from_sketch(seq)
    return (
        f"{PREFIX}{e['node_id']},{e['seq']},{elapsed_ms},{e['latitude']},"
        f"{e['longitude']},{e['temp_c']},{e['humidity_pct']},{e['hops']},"
        f"{e['battery_mv']},{e['rssi_dbm']}"
    )


# ----------------------------------------------------------------------------
# Simulated mode
# ----------------------------------------------------------------------------


def run_simulated() -> None:
    section("Line parsing")
    line = sketch_line(1, 1500)
    values = parse_data_line(line)
    check("valid [CSVDATA] line parses into 10 fields",
          values is not None and len(values) == len(FIELDS), f"got {values}")
    check("values come back in the right order",
          values is not None and dict(zip(FIELDS, values))["temp_c"] == "25.4"
          and dict(zip(FIELDS, values))["elapsed_ms"] == "1500")

    for text in [
        "ets Jun  8 2016 00:22:57",                    # ESP32 boot message
        "[INIT] LoRa radio online",                    # debug print
        "--- Forward Cycle #3 ---",                    # test sketch print
        "DATA,1,17,0,37.1,-113.5,25.4,36.5,3,3850,-87",  # old prefix
        "[CSVDATA]1,17,0,37.1,-113.5,25.4,36.5,3,3850,-87",  # missing space
        " [CSVDATA] 1,17,0,37.1,-113.5,25.4,36.5,3,3850,-87",  # not at start
        "",
    ]:
        check(f"non-data line ignored: {text!r}", parse_data_line(text) is None)

    for text in [f"{PREFIX}1,2,3", line + ",99"]:
        try:
            parse_data_line(text)
            ok = False
        except ValueError:
            ok = True
        check(f"malformed line rejected: {text!r}", ok)

    section("Serial stream -> CSV (simulated serial port)")
    tmp = Path(tempfile.mkdtemp()) / "packets.csv"
    stream = [
        "ets Jun  8 2016 00:22:57 rst:0x1 (POWERON_RESET)",
        "[INIT] base station ready",
        sketch_line(1, 1500),
        "--- Forward Cycle #2 ---",
        sketch_line(2, 5502),
        f"{PREFIX}1,2,3",  # malformed: must be skipped
        sketch_line(3, 9504),
    ]
    statuses = feed_through_loopback(stream, tmp)

    check("3 data lines written, 3 debug lines ignored, 1 bad line skipped",
          statuses == {"row": 3, "ignored": 3, "bad": 1}, f"got {statuses}")

    rows = read_csv(tmp)
    check("CSV starts with the header row", rows and rows[0] == HEADER, f"got {rows[:1]}")
    data = rows[1:]
    check("CSV has exactly 3 data rows", len(data) == 3, f"got {len(data)}")
    check("every row has a valid PC timestamp",
          all(is_iso_time(r[0]) for r in data))
    for seq, row in zip([1, 2, 3], data):
        got = dict(zip(HEADER, row))
        exp = expected_from_sketch(seq)
        mismatches = {k: (got.get(k), v) for k, v in exp.items() if got.get(k) != v}
        check(f"row {seq} values match what was sent", not mismatches, f"(got, expected): {mismatches}")

    section("Restarting the logger on an existing file")
    feed_through_loopback([sketch_line(4, 13506)], tmp)
    rows = read_csv(tmp)
    check("header is not repeated", sum(r == HEADER for r in rows) == 1)
    check("new row appended after the old ones", len(rows) == 5 and rows[-1][2] == "4",
          f"got {len(rows)} rows")


def feed_through_loopback(lines, csv_path: Path) -> dict:
    """Send `lines` through pyserial's loop:// port into handle_line()."""
    counts = {"row": 0, "ignored": 0, "bad": 0}
    f, writer = open_csv(csv_path)
    try:
        with serial.serial_for_url("loop://", timeout=0.2) as ser:
            ser.write("".join(l + "\r\n" for l in lines).encode())  # ESP32 sends CRLF
            # Hide the logger's own console output to keep the report readable
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                while raw := ser.readline():
                    counts[handle_line(raw, f, writer)] += 1
    finally:
        f.close()
    return counts


# ----------------------------------------------------------------------------
# Hardware mode
# ----------------------------------------------------------------------------


def run_hardware(port: str, baud: int, count: int, timeout_s: float, out: Path) -> None:
    section(f"Reading {count} packets from {port} @ {baud} baud (timeout {timeout_s:.0f} s)")
    print("  Live serial output:")
    if out.exists():
        out.unlink()  # Start each hardware test with a fresh CSV
    counts = {"row": 0, "ignored": 0, "bad": 0}
    f, writer = open_csv(out)
    try:
        # Opening the port resets most ESP32 boards; the sketch then reboots
        with serial.serial_for_url(port, baudrate=baud, timeout=1) as ser:
            deadline = time.monotonic() + timeout_s
            while counts["row"] < count and time.monotonic() < deadline:
                raw = ser.readline()
                if raw:
                    print("    ", end="")
                    counts[handle_line(raw, f, writer)] += 1
    except serial.SerialException as e:
        check(f"open serial port {port}", False, str(e))
        return
    finally:
        f.close()

    section("Results")
    check(f"received {count} [CSVDATA] lines before the timeout",
          counts["row"] == count, f"got {counts['row']}")
    check("debug lines were filtered out of the CSV", counts["ignored"] > 0,
          "no debug lines seen; is SerialForwarder.ino running?")
    check("no malformed [CSVDATA] lines", counts["bad"] == 0, f"{counts['bad']} bad line(s)")

    rows = read_csv(out)
    check("CSV starts with the header row", rows and rows[0] == HEADER)
    data = [dict(zip(HEADER, r)) for r in rows[1:]]
    if not data:
        return

    check("every row has a valid PC timestamp", all(is_iso_time(r["pc_time"]) for r in data))
    for r in data:
        exp = expected_from_sketch(int(r["seq"]))
        mismatches = {k: (r.get(k), v) for k, v in exp.items() if r.get(k) != v}
        check(f"seq {r['seq']}: decoded values match what the sketch encoded",
              not mismatches, f"(got, expected): {mismatches}")

    seqs = [int(r["seq"]) for r in data]
    check("sequence numbers are consecutive (no lost packets)",
          seqs == list(range(seqs[0], seqs[0] + len(seqs))), f"got {seqs}")
    elapsed = [int(r["elapsed_ms"]) for r in data]
    check("node elapsed time increases", all(b > a for a, b in zip(elapsed, elapsed[1:])),
          f"got {elapsed}")
    print(f"\n  CSV written to {out.resolve()}")


# ----------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(description="Test the serial -> CSV path.")
    parser.add_argument("--port", help="serial port of an ESP32 running SerialForwarder.ino; "
                                       "omit to run the simulated tests")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--count", type=int, default=5, help="packets to wait for (hardware)")
    parser.add_argument("--timeout", type=float, default=40, help="seconds to wait (hardware)")
    parser.add_argument("--out", type=Path, default=Path("test_packets.csv"),
                        help="CSV written during the hardware test")
    args = parser.parse_args()

    print("Serial -> CSV test (" + ("hardware: " + args.port if args.port else "simulated") + ")")
    if args.port:
        run_hardware(args.port, args.baud, args.count, args.timeout, args.out)
    else:
        run_simulated()

    passed = sum(results)
    print(f"\n{passed}/{len(results)} checks passed")
    print("ALL TESTS PASSED" if passed == len(results) and results else "SOME TESTS FAILED")
    sys.exit(0 if passed == len(results) and results else 1)


if __name__ == "__main__":
    main()
