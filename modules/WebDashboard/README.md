# WebDashboard

Live web GUI for the LoRa Rangers base station. The base station ESP32 starts
its own WiFi network and serves a dashboard at `http://192.168.4.1`. No internet
is needed in the field, and nothing loads from the web (no CDNs, fonts or
libraries).

**Owner:** Moustafa Bendari

The dashboard shows:

1. A header with the connection state, local clock, base uptime, total packets and **Download CSV**
2. An alert banner when a node is LOST: *"Node 3 LOST, last seen 14:02:11 at 37.2045, -113.6402"*
3. One card per node: status, temperature, humidity, battery, RSSI, last seen, sequence, received / missed / duplicates, loss %, position with a copy button, hop limit
4. A position map with trails, a scale bar in meters, a north arrow and per-node toggles
5. Temperature, humidity and RSSI charts (10 min / 30 min / All)
6. A packet log (newest first, filter by node, GAP rows highlighted)

Also on the page:
- **Sound alarm.** Three beeps when a node turns LOST. Use the **Sound off/on** button; the click is what lets the browser play sound.
- **Rescuer names.** Click ✎ on a card to name the person carrying that node, e.g. "Node 3 · Sam". The name shows on the card, the map and in alerts.
- **Sun mode.** A bright, high-contrast theme for reading a phone outdoors.

Names, the theme and the sound setting are saved in the browser, and so is the packet history.

Node status: **OK**, **LATE** after 60 s without a packet, **LOST** after 150 s
(nodes send every ~30 s), **NO DATA** before the first packet.

## Files

| File | What it is |
|---|---|
| `WebDashboard.h` / `.cpp` | The library: stores readings, tracks sequence numbers, serves `/` and `/data` |
| `WebDashboardPage.h` | The web page as a flash string. **Generated** from `web/index.html` |
| `WebDashboardCodec.h` | One-function bridge from Braxton's `DecodedPacket` to a `DashboardReading` |
| `WebDashboardDemo.h` | Fake traffic from 3 walking rescuers: realistic or presentation timing |
| `WebDashboard.ino` | Example sketch, presentation demo on by default (`DASH_DEMO 2`) |
| `DEMO.md` | Class presentation script: setup, schedule, talking points, Q&A |
| `web/index.html` | Editable page source (HTML + CSS + JS in one file) |
| `tools/embed_page.py` | Rebuilds `WebDashboardPage.h` from `web/index.html` |
| `tools/preview_server.py` | Runs the page on a laptop: fake data, or live from the base station's USB serial |
| `test/` | Host tests (g++ with stub Arduino/WiFi/WebServer headers) |

## Preview on a laptop (no board)

```
python tools/preview_server.py              # opens http://localhost:8000
python tools/preview_server.py --present    # class presentation schedule
python tools/preview_server.py --speed 10   # 10x faster, to see LATE/LOST sooner
```

The preview re-reads `web/index.html` on every refresh, so edit the page and reload.
The fake data uses the same scenarios as the board:

- **Realistic** (default, `DASH_DEMO 1`): packets every ~30 s, about 1 in 12 lost, one duplicate at minute 2, and Node 3 out of range from minute 4 to 7 of every 10 minutes.
- **Presentation** (`--present`, `DASH_DEMO 2`): 6x faster and fully scripted. GAP at 0:35, duplicate at 0:55, Node 3 LOST around 1:35 and back at 1:55, repeating every 2:30. See [DEMO.md](DEMO.md).

At `--speed` above 1 the times on the charts look squeezed together; that only
happens in the preview.

### Live readings on a laptop

The same page can also show **real** packets from the base station's USB
serial output (the `[CSVDATA] ...` lines from Braxton's `SerialForwarder`):

```
python -m pip install pyserial                                  # once
python tools/preview_server.py --serial COM3 --log packets.csv  # Windows (Linux: /dev/ttyUSB0)
python tools/preview_server.py --stdin                          # or type/paste lines yourself
```

It uses the real thresholds (LATE 60 s, LOST 150 s). Only one program can open
a serial port, so close the Arduino Serial Monitor and `just log` first.
`--log` writes the same CSV columns as `just log`.

After editing the page, rebuild the header before flashing:

```
python tools/embed_page.py
```

## Flash and connect

1. Open `WebDashboard.ino` in the Arduino IDE (or `just compile WebDashboard`).
   Choose `DASH_DEMO` at the top: `2` presentation demo (default), `1` realistic
   demo, `0` real packets.
2. Board: the team's default `esp32:esp32:esp32` works, and so does `esp32:esp32:heltec_wifi_lora_32_V3`. Flash it.
3. On a laptop or phone, join WiFi **LoRaRangers-Base**, password **rangers123**.
4. Open **http://192.168.4.1**.

The Serial Monitor (115200 baud, line ending "Newline") prints one log line per
accepted packet. You can also paste a CSV line there to test it, for example
`[CSVDATA] 2,17,512340,37.204512,-113.640231,25.4,31.0,3,3950,-92`. The sketch
prints whether the line was accepted and what the dashboard made of it.

## Integration

For Braxton (base station receive code). Add one line next to the existing
`forwarder.forward(...)` call:

```cpp
#include <WebDashboard.h>
#include <WebDashboardCodec.h>   // needs PacketCodec (just link-libs)

WebDashboard dashboard;          // global

void setup() {
    Serial.begin(115200);
    dashboard.begin();           // WiFi "LoRaRangers-Base" at 192.168.4.1
}

void loop() {
    dashboard.handleClient();    // every loop, never blocks

    DecodedPacket pkt;
    if (/* radio received rxBuffer */ PacketCodec::decode(rxBuffer, len, pkt)) {
        forwarder.forward(pkt, rssi);                              // PC CSV log
        dashboard.update(toDashboardReading(pkt, rssi), millis()); // <- add this
    }
}
```

If you already have the text line, this also works. The `[CSVDATA] ` prefix is optional:

```cpp
dashboard.updateFromCsv(line, millis());
```

The CSV field order is exactly the `SerialForwarder` order:
`node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,battery_mv,rssi_dbm`.

The receive loop must not block for long (no long `delay()`), or the web page
stops updating.

## How packets are classified

| Result | When | Effect |
|---|---|---|
| `NEW` | Next sequence number, or a node's first packet | Stored and shown |
| `GAP` | Sequence jumped ahead (e.g. 4 → 7) | Stored; the skipped ones count as missed |
| `DUPLICATE` | Same sequence number again (e.g. heard twice via a relay) | Counted, ignored |
| `OLD` | Up to 64 behind the latest (arrived late) | Counted, ignored |
| `BAD_NODE` | Node ID not 1..3 | Rejected |

The sequence number wraps from 65535 to 0 normally. If a node restarts (its
`elapsed_ms` goes backwards, or the sequence jumps by more than 1000), the
dashboard starts tracking it again instead of counting thousands of missed
packets.

Every packet is stamped with the base station's `millis()` when it arrives,
because node clocks are not shared. The browser turns that into a real date and
time.

## Settings

Defined in `WebDashboard.h`. Override any of them with a `#define` before the include.

| Define | Default | Meaning |
|---|---|---|
| `DASH_MAX_NODES` | 3 | Node IDs 1..N |
| `DASH_LATE_MS` / `DASH_LOST_MS` | 60000 / 150000 | Status thresholds (also `setTimeouts()`) |
| `DASH_HISTORY` | 64 | Packets kept on the board so a page refresh can rebuild history |
| `DASH_AP_SSID` / `DASH_AP_PASS` | LoRaRangers-Base / rangers123 | WiFi network |
| `DASH_JSON_BUF` | 16384 | `/data` response buffer (static RAM) |

## Web API

`GET /data?since=<id>` returns the node states plus every stored packet newer than `id`:

```json
{"nowMs":123456,"uptimeMs":123456,"bootId":918273645,"lateMs":60000,"lostMs":150000,
 "nodes":[{"id":1,"status":"OK","ageMs":4200,"seq":17,"elapsedMs":512340,
   "lat":37.204512,"lng":-113.640231,"temp":25.4,"humidity":31.0,
   "batteryMv":3950,"rssi":-92,"hopLimit":3,
   "received":40,"missed":2,"duplicates":0,"old":0,"reboots":0}],
 "packets":[{"uid":41,"node":2,"seq":17,"result":"NEW","receivedAtMs":120000,
   "elapsedMs":512340,"lat":37.204512,"lng":-113.640231,"temp":25.4,
   "humidity":31.0,"batteryMv":3950,"rssi":-92,"hopLimit":3}],
 "lastId":42,"more":false}
```

If everything doesn't fit in the buffer, `more` is `true` and the page asks
again from `lastId`. `bootId` is random on every power-up. When it changes, the
page knows the base station restarted and asks again from 0. `"mode"` appears
only when the sketch sets a label with `setModeLabel()`, as the demo modes do. The browser keeps up to 5000 packets in `localStorage`, so
a refresh doesn't wipe the history. **Clear history** empties it.

## Tests

```
test\run_tests.bat        # Windows
sh test/run_tests.sh      # macOS / Linux
```

The tests need `g++` and Python 3. They cover:

- valid and malformed CSV, node IDs 0 and 4
- gap, duplicate and old packets
- sequence wrap 65535 → 0, node reboot
- OK → LATE → LOST timing, including `millis()` wrap
- JSON validity (`python -m json.tool`), the `since` filter, the ring buffer
- tiny buffers (no overflow) and paging
- the mode label, the boot ID and the `since` edge cases
- `logLine`, and the `/`, `/data` and 404 routes
- laptop live mode (`test/test_live_feed.py`): the same good and bad CSV lines as
  the C++ parser, counters, `nan` sent as JSON `null`, and the `--log` CSV format
