#ifndef PACKET_CODEC_H
#define PACKET_CODEC_H

#include <Arduino.h>

// Exact 32-byte agreed team packet structure
struct __attribute__((packed)) AgreedPacket {
    uint16_t nodeId;     // 2 bytes: Identifies the monitoring node
    uint16_t seqNum;     // 2 bytes: Identifies each new packet
    int32_t timestampMs; // 4 bytes: Time elapsed since node powered (ms)
    double longitude;    // 8 bytes: GPS longitude
    double latitude;     // 8 bytes: GPS latitude
    int16_t temperature; // 2 bytes: Temperature (scaled x10)
    uint16_t humidity;   // 2 bytes: Humidity (scaled x10)
    uint8_t hops;        // 1 byte:  Hop counter to prevent endless loops
    uint16_t batteryMv;  // 2 bytes: Battery voltage (mV)
    uint8_t checksum;    // 1 byte:  CRC-8 checksum over the first 31 bytes
};

// Packet contents unpacked back into real units
struct DecodedPacket {
    uint16_t nodeId;
    uint16_t seqNum;
    int32_t timestampMs;
    double longitude;
    double latitude;
    float tempC;  // Degrees Celsius
    float humPct; // Relative humidity (%)
    uint8_t hops;
    uint16_t batteryMv;
};

class PacketCodec {
  public:
    static const uint8_t PACKET_SIZE = sizeof(AgreedPacket); // 32 bytes

    // Compresses float sensor readings and packs all fields into a 32-byte
    // buffer
    static uint8_t encode(uint16_t nodeId, uint16_t seqNum, int32_t timestampMs,
                          double lon, double lat, float tempC, float humPct,
                          uint8_t hops, uint16_t batteryMv, uint8_t *outBuffer);

    // Validates the length and checksum, then unpacks the buffer into `out`
    // with temperature and humidity scaled back to real units. Returns false
    // (leaving `out` untouched) if the packet is invalid.
    static bool decode(const uint8_t *packetBuffer, uint8_t len,
                       DecodedPacket &out);

    // Validates the checksum, decrements the hop count, and recalculates the
    // checksum in-place
    static bool decrementHops(uint8_t *packetBuffer, uint8_t len);

    // Computes an 8-bit CRC checksum (polynomial 0x07) over the first 31 bytes
    static uint8_t computeChecksum(const uint8_t *data, uint8_t len);

    // Prints the 32-byte buffer in hex format to the Serial Monitor
    static void printHexBuffer(const uint8_t *buffer, uint8_t len);
};

#endif // PACKET_CODEC_H
