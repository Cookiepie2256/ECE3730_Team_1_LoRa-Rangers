// Prints this board's MAC address and node ID. Flash it on each node once and
// check that no two boards print the same ID.

#include "NodeId.h"

void setup() {
    Serial.begin(115200);
    delay(500);
}

void loop() {
    uint8_t mac[6];
    NodeId::readMac(mac);
    const uint16_t id = NodeId::get();
    Serial.printf("MAC %02X:%02X:%02X:%02X:%02X:%02X  ->  node ID %u (0x%04X)\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned)id,
                  (unsigned)id);
    delay(2000);
}
