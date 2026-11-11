# /// script
# requires-python = ">=3.9"
# dependencies = ["pyserial>=3.5"]
# ///
"""Record packets from the base station ESP32 into a CSV file on this PC.

The base station prints one line per packet (see modules/SerialForwarder):

    [CSVDATA] node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,battery_mv,rssi_dbm

Only lines starting with "[CSVDATA] " are written to the CSV, with the PC's date
and time added as the first column. Every other line (debug prints, boot
messages) is echoed to the terminal and otherwise ignored.

Usage:
    uv run tools/serial_to_csv.py /dev/ttyUSB0          # Linux
    uv run tools/serial_to_csv.py COM3 --out run1.csv   # Windows
"""

import argparse
import csv
import sys
import time
from datetime import datetime
from pathlib import Path

import serial

# Must match the field order in modules/SerialForwarder/SerialForwarder.cpp
FIELDS = [
    "node_id",
    "seq",
    "elapsed_ms",
    "latitude",
    "longitude",
    "temp_c",
    "humidity_pct",
    "hops",
    "battery_mv",
    "rssi_dbm",
]
HEADER = ["pc_time", *FIELDS]
# Must match SerialForwarder::PREFIX (including the trailing space)
PREFIX = "[CSVDATA] "


def parse_data_line(line: str):
    """Return the field values of a [CSVDATA] line, or None if it isn't one."""
    if not line.startswith(PREFIX):
        return None
    values = line[len(PREFIX):].strip().split(",")
    if len(values) != len(FIELDS):
        raise ValueError(f"expected {len(FIELDS)} fields, got {len(values)}")
    return values


def open_csv(path: Path):
    """Open `path` for appending, writing the header if the file is new/empty."""
    is_new = not path.exists() or path.stat().st_size == 0
    f = path.open("a", newline="", encoding="utf-8")
    writer = csv.writer(f)
    if is_new:
        writer.writerow(HEADER)
        f.flush()
    return f, writer


def record(port: str, baud: int, out_path: Path) -> None:
    f, writer = open_csv(out_path)
    rows = 0
    print(f"Logging {port} @ {baud} baud -> {out_path} (Ctrl+C to stop)")
    try:
        while True:
            try:
                # serial_for_url also accepts test URLs like loop://
                with serial.serial_for_url(port, baudrate=baud, timeout=1) as ser:
                    while True:
                        raw = ser.readline()
                        if not raw:
                            continue
                        line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                        try:
                            values = parse_data_line(line)
                        except ValueError as e:
                            print(f"[skipped bad CSVDATA line: {e}] {line}", file=sys.stderr)
                            continue
                        if values is None:
                            print(line)  # Debug output from the ESP32
                            continue
                        pc_time = datetime.now().isoformat(timespec="seconds")
                        writer.writerow([pc_time, *values])
                        f.flush()  # Keep the file current if the script is killed
                        rows += 1
                        print(f"[row {rows}] {line}")
            except serial.SerialException as e:
                print(f"[serial error: {e}; retrying in 2 s]", file=sys.stderr)
                time.sleep(2)
    except KeyboardInterrupt:
        print(f"\nStopped. {rows} row(s) written to {out_path}")
    finally:
        f.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("port", help="serial port, e.g. /dev/ttyUSB0 or COM3")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--out", type=Path, default=Path("packets.csv"))
    args = parser.parse_args()
    record(args.port, args.baud, args.out)


if __name__ == "__main__":
    main()
