#include "SerialForwarder.h"

// Test: send fake decoded packets to the PC. Record them with
//   just log <port>
// Only the [CSVDATA] lines end up in the CSV; the other prints are ignored.
SerialForwarder forwarder(Serial);

uint16_t sequence = 1;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("==================================================");
    Serial.println(" ECE-3730: SerialForwarder Independent Test");
    Serial.println("==================================================");
}

void loop() {
    Serial.printf("\n--- Forward Cycle #%u ---\n", sequence);

    // Simulate a received frame: encode fake data, then decode it as the base
    // station would
    uint8_t frame[PacketCodec::PACKET_SIZE];
    uint8_t len = PacketCodec::encode(sequence % 3 + 1, sequence, millis(),
                                      -113.584, 37.104, 25.4f, 36.5f, 3, 3850,
                                      frame);

    DecodedPacket pkt;
    if (!PacketCodec::decode(frame, len, pkt)) {
        Serial.println("[DECODE] ERROR: Invalid packet, not forwarded.");
    } else if (!forwarder.forward(pkt, -70 - (int16_t)(sequence % 30))) {
        Serial.println("[FWD]    ERROR: Failed to send [CSVDATA] line.");
    }

    sequence++;
    delay(4000);
}
