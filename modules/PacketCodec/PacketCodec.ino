#include "PacketCodec.h"

uint16_t sequence = 1;

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("==================================================");
    Serial.println(" ECE-3730: PacketCodec Independent Test");
    Serial.println("==================================================");
}

void loop() {
    Serial.printf("\n--- Test Cycle #%u ---\n", sequence++);

    uint8_t txBuffer[PacketCodec::PACKET_SIZE];
    
    // 1. Encode the packet (Node 3 originating data)
    uint8_t written = PacketCodec::encode(
        3, sequence, millis(), -113.584, 37.104, 
        25.4f, 36.5f, 3, 3850, txBuffer
    );
    
    Serial.printf("[ENCODE] Generated 32-byte frame | Hops set to: %u\n", txBuffer[28]);
    Serial.print("         Hex: ");
    PacketCodec::printHexBuffer(txBuffer, written);

    // 2. Simulate Relay Node Interception
    Serial.println("[RELAY]  Simulating intercept at intermediate node...");
    
    if (PacketCodec::decrementHops(txBuffer, written)) {
        Serial.printf("[RELAY]  Valid CRC. Hops decremented to: %u\n", txBuffer[28]);
        Serial.println("[RELAY]  New CRC-8 calculated successfully.");
    } else {
        Serial.println("[RELAY]  ERROR: Packet dropped (Invalid CRC or Hops=0).");
    }

    // 3. Simulate Hop Exhaustion
    Serial.println("[TEST]   Forcing Hops to 0...");
    txBuffer[28] = 0; 
    txBuffer[31] = PacketCodec::computeChecksum(txBuffer, 31); // fix checksum so it doesn't fail on CRC
    if (!PacketCodec::decrementHops(txBuffer, written)) {
        Serial.println("[TEST]   PASSED: Packet correctly dropped when Hops = 0.");
    }

    delay(4000);
}