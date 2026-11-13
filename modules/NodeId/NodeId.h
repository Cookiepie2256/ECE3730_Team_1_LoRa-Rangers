#ifndef NODE_ID_H
#define NODE_ID_H

// NodeId: gives each node a unique ID read from the ESP32 at runtime, so every
// node can run the same firmware with nothing to set per board.
//
// The ID is the last 2 bytes of the factory MAC address burned into eFuse.
// The first 3 bytes are Espressif's vendor prefix (the same on every board),
// so the last bytes are the part that differs. Two boards could share an ID
// (about 1 in 65,000 per pair); NodeId.ino prints the ID so you can check.
//
// The base station's WebDashboard gives each new ID its own slot the first
// time it hears from it.

#include <Arduino.h>

namespace NodeId {

// The factory MAC address from eFuse. Works before WiFi or LoRa is started.
void readMac(uint8_t mac[6]);

// This board's node ID: the last 2 bytes of the MAC (e.g. ...:BB:01 -> 0xBB01).
// Never 0 (0 means "no node"); a MAC ending in 00:00 gives 1 instead.
uint16_t get();

} // namespace NodeId

#endif // NODE_ID_H
