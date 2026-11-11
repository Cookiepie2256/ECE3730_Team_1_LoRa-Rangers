## Project Overview
A fun team project with a search-and-rescue connection

## Team Members
| Name | GitHub Username | Responsibility |
|------|-----------------|----------------|
| Anthony | angottianthony | GPS code |
| Mo | Moustafa-Bendari | Dashboards and presentation |
| Savannah | savannahwalker786 | DHT11s |
| Braxton | ThatOneShortGuy | network / decoding |
| Rowan | Romanhill7 | network / encoding |

## Individual Contribution: Braxton Brown

### My Responsibility
I will work on the receiving part of the network: decode packets on the base station, send them to a PC over USB serial, and record them to a CSV file on the PC for the dashboard to read.

### Data Flow
```
[Sensor nodes] --LoRa--> [Base station ESP32] --USB serial--> [PC: just log] --> packets.csv
                          PacketCodec::decode()                serial_to_csv.py
                          SerialForwarder::forward()
```
The base station does not store anything; the PC writes the CSV and adds the real date and time to each row (the nodes only know time since their own startup).

### My Files
- `modules/PacketCodec/PacketCodec.h` / `PacketCodec.cpp` — `PacketCodec::decode()` and the `DecodedPacket` struct (added to the shared codec alongside the encoder)
- `modules/PacketCodec/PacketCodec.ino` — test sketch, extended with decode and corrupted-packet checks
- `modules/SerialForwarder/` — sends each decoded packet to the PC as one `[CSVDATA] ...` text line; includes a test sketch
- `tools/serial_to_csv.py` — PC program that reads the serial port and writes `[CSVDATA]` lines to a CSV (run with `just log <port>`)
- `justfile`, `.clangd`, `modules/*/sketch.yaml` — shared build, LSP and formatting setup for the team

### My Inputs and Outputs
| Direction | Data | Type / Units / Format | Source or Destination |
|-----------|------|-----------------------|-----------------------|
| Input | Encoded packet | 32-byte binary frame (`AgreedPacket`, CRC-8 in last byte) | LoRa radio (network) |
| Output | Decoded packet | `DecodedPacket` struct: node ID, sequence #, elapsed ms, lon/lat (degrees), temperature (°C), humidity (%RH), hops, battery (mV) | Base station code |
| Output | Serial line | Text: `[CSVDATA] node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,battery_mv,rssi_dbm` at 115200 baud | PC over USB serial |
| Output | CSV log | CSV with header; columns `pc_time` (ISO 8601) followed by the serial line's fields | `packets.csv` on the PC (dashboard) |

### How to Use the Decoder
```cpp
DecodedPacket pkt;
if (PacketCodec::decode(rxBuffer, len, pkt)) {
    // use pkt.nodeId, pkt.tempC, pkt.humPct, ...
}
```
`decode()` returns `false` and leaves `pkt` unchanged if the buffer is null, is not exactly 32 bytes, or fails the CRC check. Temperature and humidity are converted back from their ×10 encoding to real units.

### How to Forward and Record Packets
On the base station:
```cpp
#include <SerialForwarder.h>

SerialForwarder forwarder(Serial); // after Serial.begin(115200)

DecodedPacket pkt;
if (PacketCodec::decode(rxBuffer, len, pkt)) {
    forwarder.forward(pkt, rssi);
}
```
On the PC, with the base station plugged in:
```
just log /dev/ttyUSB0        # Linux (Windows: just log COM3)
```
Only lines starting with `[CSVDATA] ` go into the CSV, so other `Serial` prints on the base station are safe. Malformed `[CSVDATA]` lines are skipped with a warning. Rerunning appends to the same file without repeating the header. If the prefix or field list changes, update both `SerialForwarder.cpp` and `PREFIX`/`FIELDS` in `serial_to_csv.py`.

### My Test Plan
- **Round trip:** encode known values, decode them, and check that every field comes back exactly (done on host and in `PacketCodec.ino`).
- **Edge cases:** negative temperatures, humidity clamped to 100%, wrong-length and null buffers (done on host).
- **Corruption:** flip a byte in a valid packet and confirm `decode()` rejects it (done on host and in `PacketCodec.ino`).
- **Serial to CSV (no hardware):** fed `SerialForwarder` output mixed with boot noise, debug prints and a malformed `[CSVDATA]` line through a virtual serial port into `just log`; only valid rows were written, the header appeared once across reruns, and Ctrl+C stopped cleanly (done on host).
- **Next:** flash `SerialForwarder.ino` and record with `just log` on real hardware, then decode real frames received over LoRa from the other nodes.
