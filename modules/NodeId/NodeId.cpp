#include "NodeId.h"

#include <esp_mac.h>

namespace NodeId {

void readMac(uint8_t mac[6]) {
    // Reads eFuse directly; WiFi.macAddress() can return all zeros before
    // WiFi is started.
    esp_efuse_mac_get_default(mac);
}

uint16_t get() {
    uint8_t mac[6];
    readMac(mac);
    const uint16_t id = ((uint16_t)mac[4] << 8) | mac[5];
    return id == 0 ? 1 : id;
}

} // namespace NodeId
