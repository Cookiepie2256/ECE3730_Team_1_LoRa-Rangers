
# Team Project: ECE3730_Team_1_LoRa Rangers

## Project Overview
A fun team project with a search-and-rescue connection

## Team Members
| Name | GitHub Username | Responsibility |
|------|-----------------|----------------|
| Moustafa Bendari | Moustafa-Bendari | Web dashboard (live display of node data) and presentation |

## Individual Contribution: Moustafa Bendari

### My Responsibility
I built the **WebDashboard**, the GUI that runs on the base station ESP32. The base station starts its own WiFi network (`LoRaRangers-Base`) and serves a dashboard at `http://192.168.4.1`. A team lead joins that network on a laptop or phone and watches every rescuer live. The page works fully offline: no internet, CDNs or external libraries.

The dashboard takes each packet that Braxton's decoder produces and:
- stamps it with the base station's time on arrival (node clocks are not shared)
- checks the sequence number to find **missed**, **duplicate** and **late (old)** packets, including the 65535 → 0 wrap and node restarts
- marks each node **OK**, **LATE** (no packet for 60 s) or **LOST** (150 s), since nodes send every ~30 s
- serves a web page with a LOST alert banner, one card per node (temperature, humidity, battery, signal, last seen, packet loss, GPS position), a position map with trails, temperature/humidity/RSSI charts, a packet log, and a CSV download

### My Files
All files are in `modules/WebDashboard/`. See its `README.md` for details.
- `WebDashboard.h` — The `WebDashboard` class, the `DashboardReading` struct, the `UpdateResult` / `NodeStatus` enums, and the settings (`DASH_LATE_MS`, `DASH_LOST_MS`, WiFi name...).
- `WebDashboard.cpp` — Sequence tracking, LATE/LOST status, the 64-packet history ring buffer, CSV parsing, JSON output, and the WiFi access point and web routes (`/`, `/data`).
- `WebDashboardPage.h` — The web page stored in flash (generated from `web/index.html` by `tools/embed_page.py`).
- `WebDashboardCodec.h` — `toDashboardReading()`, which turns the team's `DecodedPacket` into a dashboard reading in one call.
- `WebDashboardDemo.h` / `WebDashboard.ino` — Example sketch with a demo mode: 3 fake rescuers walking near Snow Canyon, Utah, with lost packets, a duplicate, and Node 3 dropping out (LATE → LOST → recovers).
- `web/index.html` — The page source (HTML, CSS and JavaScript in one file).
- `tools/preview_server.py` — Runs the page on a laptop with the same fake data, no board needed.
- `test/` — Host tests compiled with g++ against stub Arduino/WiFi/WebServer headers.

### My Inputs and Outputs
| Direction | Data | Type / Units / Format | Source or Destination |
|-----------|------|-----------------------|-----------------------|
| Input | Decoded packet | `DecodedPacket` → `DashboardReading`: node ID, seq #, elapsed ms, lat/lng (degrees), temperature (°C), humidity (%RH), hops, battery (mV), plus RSSI (dBm) | Braxton's `PacketCodec::decode()` on the base station, via `dashboard.update(toDashboardReading(pkt, rssi), millis())` |
| Input | CSV line (alternative) | Text: `[CSVDATA] node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,battery_mv,rssi_dbm` (same order as `SerialForwarder`) | `dashboard.updateFromCsv(line, millis())`, or typed into the Serial Monitor for testing |
| Input | Arrival time | `millis()` of the base station (ms) | Base station clock |
| Output | Update result | `UpdateResult`: NEW, GAP, DUPLICATE, OLD, BAD_NODE | Calling code / Serial |
| Output | Node status | `NodeStatus`: NO_DATA, OK, LATE (≥ 60 s), LOST (≥ 150 s) | Web page, `getStatus()` |
| Output | Live dashboard | Web page at `http://192.168.4.1` (WiFi `LoRaRangers-Base`) | Team lead's browser (laptop or phone) |
| Output | JSON data | `GET /data?since=<id>`: node states + packets newer than `id` | The web page (polls every 1 s) |
| Output | Log line | Text CSV `DASH,received_at_ms,node_id,seq,result,...` | Serial Monitor or SD card |
| Output | CSV file | `update,timestamp,node,seq,result,lat,lng,temp_c,humidity_pct,battery_mv,rssi_dbm` | Download CSV button in the browser |

### My Test Plan
- **Host tests (no hardware):** compile the library with g++ and stub ESP32 headers, then feed it readings and CSV lines. Check the result of each, the counters, the node status at exact times, and the JSON output.
- **JSON validity:** every JSON response the tests produce is checked with `python -m json.tool`, including responses built in buffers that are deliberately too small.
- **Browser preview (no hardware):** run `tools/preview_server.py` and check every section of the page with live fake data, on a laptop-sized and a phone-sized screen.
- **ESP32 compile:** `arduino-cli compile` for the team's board (`esp32:esp32:esp32`) and the Heltec V3.
- **On hardware (next):** flash `WebDashboard.ino`, join `LoRaRangers-Base`, open `192.168.4.1`, and record the demo. Then paste Braxton's real `[CSVDATA]` lines into the Serial Monitor, and finally add the one `update()` line to the base station receive code.

### Test Results (November 12, 2026)
| Test | Result |
|------|--------|
| Valid CSV, with and without the `[CSVDATA]` prefix, negative temperature, `nan` sensor values | Pass |
| 20 malformed CSV lines (wrong field count, empty fields, junk, out-of-range lat/lng/humidity/seq/hops/battery/RSSI, too long, null) | Pass: all rejected, nothing stored |
| Node IDs 0 and 4 | Pass: rejected as `BAD_NODE` |
| Gap (seq 2 → 5) | Pass: `GAP`, 2 missed |
| Duplicate / old packet | Pass: ignored and counted; "last seen" not refreshed |
| Sequence wrap 65535 → 0 (and 65534 → 1) | Pass: `NEW` with 0 missed (and `GAP` with 2 missed) |
| Node reboot (seq 500 → 1, clock back to 2 s) | Pass: tracking reset, 0 missed |
| OK → LATE → LOST timing (59 999 / 60 000 / 149 999 / 150 000 ms) and `millis()` wrap | Pass |
| `buildJson` valid JSON, `since` filter, 64-packet ring buffer | Pass: 8 JSON files valid in `python -m json.tool` |
| Tiny buffers (0 to 3000 bytes) | Pass: no byte written past the buffer; paging delivers all 30 packets |
| Web routes `/`, `/data`, unknown path | Pass: 200 page, 200 JSON, 404 |
| **Total host tests** | **207 checks passed, 0 failed** |
| ESP32 compile, `esp32:esp32:esp32` (core 3.3.12) | Pass: 977 215 bytes flash (74%), 68 072 bytes RAM (20%) |
| ESP32 compile, `heltec_wifi_lora_32_V3` | Pass: 939 624 bytes flash (28%), 66 736 bytes RAM (20%) |
| Browser preview: all 6 sections, LOST alert, refresh keeps history, phone layout without sideways scrolling | Pass |
| On real hardware | Not done yet |
