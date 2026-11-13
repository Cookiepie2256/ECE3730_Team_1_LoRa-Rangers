// Host-side stand-in for the ESP32 WiFi library (access point calls only)
#ifndef STUB_WIFI_H
#define STUB_WIFI_H

#include "Arduino.h"

enum WiFiMode_t { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA };

class IPAddress {
  public:
    IPAddress(uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0)
        : a_(a), b_(b), c_(c), d_(d) {}
    String toString() const {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", a_, b_, c_, d_);
        return String(buf);
    }

  private:
    uint8_t a_, b_, c_, d_;
};

class WiFiClass {
  public:
    bool mode(WiFiMode_t m) {
        mode_ = m;
        return true;
    }
    bool softAPConfig(IPAddress ip, IPAddress, IPAddress) {
        ip_ = ip;
        return true;
    }
    bool softAP(const char *ssid, const char *pass) {
        ssid_ = ssid ? ssid : "";
        // Like the real library: WPA2 needs 8+ characters
        return pass == nullptr || std::strlen(pass) >= 8;
    }
    IPAddress softAPIP() const { return ip_; }

    WiFiMode_t mode_ = WIFI_OFF;
    std::string ssid_;
    IPAddress ip_;
};

inline WiFiClass WiFi;

#endif // STUB_WIFI_H
