 
## Individual Contribution: Roman Hill

### My Responsibility
Compressing and sending data from the three node controllers to the MCU

### My Files
- `FileName.h` — What this file will contain.
- `FileName.cpp` — What this file will implement.

### My Inputs and Outputs
| Direction | Data | Type / Units / Format | Source or Destination |
|-----------|------|-----------------------|-----------------------|
| Input | Raw sensor data | Raw ADC value | [Source] |
| Output | compressed data | 12 bytes of data| [Destination] |

### My Test Plan
I will compress and send fake data and make sure it is being compressed correctly and sent in lossless quality.

```

**Markdown tips:** `#` creates a title, `##` creates a section heading, `###` creates a subsection, and `-` creates a bullet. Backticks format filenames or code. On GitHub, open the README to see the formatted result.

# Individual Library Documentation: Packet Codec & Mesh Relay Transport
**ECE 3730 | Embedded Systems 1 | Monitoring System Project | Assignment 1**

| Information | Entry |
|---|---|
| **Student** | Roman Hill |
| **Team** | [Team Name or Letter] |
| **Module** | Packet Encoding & Relaying (`PacketCodec` & `LoRaTx`) |
| **Date** | November 9, 2026 |
| **Repository link** | [Team GitHub Repository Link] |

---

## 1. Library Architecture, Use, and Interface
The `PacketCodec` and `LoRaTx` modules provide the encoding and multi-hop relay mechanics for the distributed sensor network. To fulfill the strict node responsibilities, these modules **do not unpack or decode data**. Their sole purposes are to compress sensor telemetry into a 32-byte binary frame, compute a polynomial `0x07` CRC-8 checksum, manage the custom TTL `hops` field, and broadcast the frame using CSMA Listen-Before-Talk via the SX1276 LoRa transceiver.

### Public API Reference
| Class & Method | Inputs | Returns / Output | Architectural Purpose |
| :--- | :--- | :--- | :--- |
| `PacketCodec::encode` | `uint16_t nodeId, seqNum, batteryMv`, `int32_t timestampMs`, `double lon, lat`, `float tempC, humPct`, `uint8_t hops`, `uint8_t* outBuffer` | `uint8_t` (Bytes Written: `32`) | Compresses floating-point telemetry into fixed-point integers, packs the 10 fields into exactly 32 bytes, and computes the CRC-8 checksum. |
| `PacketCodec::decrementHops` | `uint8_t* packetBuffer`, `uint8_t len` | `bool` (`true` if relayed) | Validates the incoming frame's CRC-8, subtracts `1` from the `hops` field (byte 28), recalculates the CRC in-place, and approves the frame for rebroadcast. |
| `LoRaTx::begin` | `int8_t txPowerDbm = 20`, `uint16_t cadTimeoutMs = 500` | `bool` (`true` if online) | Initializes the SPI bus, sets 915 MHz frequency, 20 dBm TX power, and enables CSMA Channel Activity Detection. |
| `LoRaTx::transmitFrame` | `const uint8_t* frameBuffer`, `uint8_t len` | `bool` (`true` if sent) | Blocks until the RF channel is clear (LBT/CAD), then broadcasts the 32-byte frame. |
| `LoRaTx::processRelay` | *(None)* | `bool` (`true` if relayed) | Intercepts incoming 32-byte frames, calls `decrementHops`, and re-transmits the modified frame if the TTL is valid. |

---

## 2. Rigorous Testing, Verification, and Protocol Performance
The software modules were exercised using dedicated unit-test sketches (`PacketCodec.ino` and `LoRaTx.ino`) running on an ESP32 microcontroller connected via SPI to an SX1276 LoRa transceiver operating at 915.0 MHz.

### Test Matrix and Validation Metrics
| Test Category | Experimental Setup & Procedure | Expected Protocol Behavior | Observed Results |
| :--- | :--- | :--- | :--- |
| **Memory Alignment** | Executed `PacketCodec.ino` to evaluate `sizeof(AgreedPacket)` on the 32-bit ESP32 toolchain. | Structure must compile to exactly 32 bytes without compiler padding. | `sizeof(AgreedPacket)` evaluated to `32` bytes, successfully mapping to the team's agreed data frame. |
| **Hop Management** | Fed a valid 32-byte frame with `hops=3` into `PacketCodec::decrementHops()`. | Function must drop `hops` to `2` and update the checksum byte. | Hops correctly decremented; CRC recalculated seamlessly. |
| **Hop Exhaustion** | Fed a 32-byte frame with `hops=0` into the relay function. | Relay logic must return `false` and drop the packet. | `decrementHops()` aborted execution, successfully preventing infinite loops. |
| **CRC-8 Error Rejection** | Flipped a single bit in the 32-byte buffer before passing it to the relay logic. | Relay node must detect the checksum mismatch and drop the frame. | 100% of corrupted packets were dropped; false rebroadcasts were prevented. |
| **CSMA Channel Access** | Executed `LoRaTx.ino`'s `transmitFrame()` during simulated RF contention. | Radio must execute CAD (Listen-Before-Talk) and delay transmission. | Transceiver successfully paused transmission until the channel preamble cleared. |

---

## 3. Integration Guidelines
These libraries are fully implemented and ready for integration into the sender and relay nodes.

| Item | Notes |
|---|---|
| **Working** | 32-byte fixed-point encoding, polynomial `0x07` CRC-8 calculation, in-place hop decrementing, and CSMA channel scanning. |
| **Integration limits** | Central MCU display logic must implement its own unpacking routine, as this module is strictly responsible for encoding, TTL management, and transmission. |
| **Hardware** | Requires SPI connections (`MOSI=23`, `MISO=19`, `SCK=5`, `CS=18`) and interrupt pin (`DIO0=26`) on the ESP32. |