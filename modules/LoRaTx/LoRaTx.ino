#include "LoRaTx.h"

LoRaTx loraRadio(18, 26, 915.0f);
uint16_t seq = 1;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("==================================================");
    Serial.println(" ECE-3730: LoRaTx Independent Test");
    Serial.println("==================================================");

    if (loraRadio.begin()) {
        Serial.println("[INIT] LoRa Radio Online.");
    } else {
        Serial.println("[INIT] LoRa Radio offline. Running simulation.");
    }
}

void loop() {
    Serial.printf("\n--- Transmission Cycle #%u ---\n", seq++);

    // Dummy 32-byte frame matching the agreed struct (Hops at index 28)
    uint8_t dummyFrame[32] = {0};
    dummyFrame[28] = 3; // Hops = 3
    dummyFrame[31] = PacketCodec::computeChecksum(dummyFrame, 31);

    Serial.println("[TX]     Broadcasting 32-byte frame via CSMA...");
    if (loraRadio.transmitFrame(dummyFrame, 32)) {
        Serial.println("[TX OK]  Frame transmitted successfully.");
    }

    Serial.println("[LISTEN] Checking for incoming mesh traffic to relay...");
    if (loraRadio.processRelay()) {
        Serial.println("[RELAY]  Received frame, decremented hops, and rebroadcasted.");
    } else {
        Serial.println("[RELAY]  No valid traffic requiring relaying.");
    }

    delay(4000);
}