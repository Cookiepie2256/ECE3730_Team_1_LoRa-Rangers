#include "PacketCodec.h"
#include <math.h>

uint8_t PacketCodec::encode(uint16_t nodeId, uint16_t seqNum,
                            int32_t timestampMs, double lon, double lat,
                            float tempC, float humPct, uint8_t hops,
                            uint16_t batteryMv, uint8_t *outBuffer) {
    if (outBuffer == nullptr)
        return 0;

    AgreedPacket pkt;
    pkt.nodeId = nodeId;
    pkt.seqNum = seqNum;
    pkt.timestampMs = timestampMs;
    pkt.longitude = lon;
    pkt.latitude = lat;
    pkt.temperature = (int16_t)round(tempC * 10.0f);

    if (humPct < 0.0f)
        humPct = 0.0f;
    if (humPct > 100.0f)
        humPct = 100.0f;
    pkt.humidity = (uint16_t)round(humPct * 10.0f);

    pkt.hops = hops;
    pkt.batteryMv = batteryMv;
    pkt.checksum =
        computeChecksum((const uint8_t *)&pkt, sizeof(AgreedPacket) - 1);

    memcpy(outBuffer, &pkt, sizeof(AgreedPacket));
    return sizeof(AgreedPacket);
}

bool PacketCodec::decode(const uint8_t *packetBuffer, uint8_t len,
                         DecodedPacket &out) {
    if (packetBuffer == nullptr || len != sizeof(AgreedPacket)) {
        return false;
    }

    AgreedPacket pkt;
    memcpy(&pkt, packetBuffer, sizeof(AgreedPacket));

    uint8_t expectedCrc =
        computeChecksum((const uint8_t *)&pkt, sizeof(AgreedPacket) - 1);
    if (pkt.checksum != expectedCrc) {
        return false; // Corrupted packet
    }

    out.nodeId = pkt.nodeId;
    out.seqNum = pkt.seqNum;
    out.timestampMs = pkt.timestampMs;
    out.longitude = pkt.longitude;
    out.latitude = pkt.latitude;
    out.tempC = pkt.temperature / 10.0f;
    out.humPct = pkt.humidity / 10.0f;
    out.hops = pkt.hops;
    out.batteryMv = pkt.batteryMv;

    return true;
}

bool PacketCodec::decrementHops(uint8_t *packetBuffer, uint8_t len) {
    if (packetBuffer == nullptr || len != sizeof(AgreedPacket)) {
        return false;
    }

    AgreedPacket pkt;
    memcpy(&pkt, packetBuffer, sizeof(AgreedPacket));

    uint8_t expectedCrc =
        computeChecksum((const uint8_t *)&pkt, sizeof(AgreedPacket) - 1);
    if (pkt.checksum != expectedCrc) {
        return false; // Corrupted packet, do not relay
    }

    if (pkt.hops == 0) {
        return false; // Hop limit reached, drop packet
    }

    pkt.hops -= 1;
    pkt.checksum =
        computeChecksum((const uint8_t *)&pkt, sizeof(AgreedPacket) - 1);
    memcpy(packetBuffer, &pkt, sizeof(AgreedPacket));

    return true;
}

uint8_t PacketCodec::computeChecksum(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80)
                crc = (uint8_t)((crc << 1) ^ 0x07);
            else
                crc <<= 1;
        }
    }
    return crc;
}

void PacketCodec::printHexBuffer(const uint8_t *buffer, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        Serial.printf("%02X ", buffer[i]);
    }
    Serial.println();
}
