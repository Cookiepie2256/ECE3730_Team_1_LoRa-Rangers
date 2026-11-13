// Host-side stand-in for the parts of Arduino.h that WebDashboard uses, so
// the dashboard logic compiles and runs with plain g++ (no ESP32 toolchain).
#ifndef STUB_ARDUINO_H
#define STUB_ARDUINO_H

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define PROGMEM
typedef const char *PGM_P;

// Tests set this to control the clock seen by WebDashboard::handleData()
inline uint32_t &stubMillis() {
    static uint32_t ms = 0;
    return ms;
}
inline uint32_t millis() { return stubMillis(); }

inline long random(long lo, long hi) { return hi > lo ? lo + std::rand() % (hi - lo) : lo; }
inline long random(long hi) { return random(0, hi); }

class String {
  public:
    String(const char *s = "") : s_(s ? s : "") {}
    String(const std::string &s) : s_(s) {}
    const char *c_str() const { return s_.c_str(); }
    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }

  private:
    std::string s_;
};

#endif // STUB_ARDUINO_H
