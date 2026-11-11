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
I will work on the receiving part of the network and decode packets into data for the dashboard to read.

### My Files
- `modules/PacketCodec/PacketCodec.h` / `PacketCodec.cpp` — `PacketCodec::decode()` and the `DecodedPacket` struct (added to the shared codec alongside the encoder)
- `modules/PacketCodec/PacketCodec.ino` — test sketch, extended with decode and corrupted-packet checks
- `justfile`, `.clangd`, `modules/*/sketch.yaml` — shared build, LSP and formatting setup for the team

### My Inputs and Outputs
| Direction | Data | Type / Units / Format | Source or Destination |
|-----------|------|-----------------------|-----------------------|
| Input | Encoded packet | 32-byte binary frame (`AgreedPacket`, CRC-8 in last byte) | LoRa radio (network) |
| Output | Decoded packet | `DecodedPacket` struct: node ID, sequence #, elapsed ms, lon/lat (degrees), temperature (°C), humidity (%RH), hops, battery (mV) | Dashboard / CSV logging |
| Output | CSV log *(planned)* | CSV, one row per packet | CSV file |

### How to Use the Decoder
```cpp
DecodedPacket pkt;
if (PacketCodec::decode(rxBuffer, len, pkt)) {
    // use pkt.nodeId, pkt.tempC, pkt.humPct, ...
}
```
`decode()` returns `false` and leaves `pkt` unchanged if the buffer is null, is not exactly 32 bytes, or fails the CRC check. Temperature and humidity are converted back from their ×10 encoding to real units.

### My Test Plan
- **Round trip:** encode known values, decode them, and check that every field comes back exactly (done on host and in `PacketCodec.ino`).
- **Edge cases:** negative temperatures, humidity clamped to 100%, wrong-length and null buffers (done on host).
- **Corruption:** flip a byte in a valid packet and confirm `decode()` rejects it (done on host and in `PacketCodec.ino`).
- **Next:** decode real frames received over LoRa from the other nodes, then write the decoded data to a CSV file.
