#ifndef LORA_TX_H
#define LORA_TX_H

#include <Arduino.h>
#include <PacketCodec.h>
#include <RH_RF95.h>
#include <SPI.h>

class LoRaTx {
  public:
    LoRaTx(uint8_t csPin = 18, uint8_t intPin = 26, float freqMhz = 915.0f);

    bool begin(int8_t txPowerDbm = 20, uint16_t cadTimeoutMs = 500);

    // Transmits a 32-byte frame using CSMA collision avoidance
    bool transmitFrame(const uint8_t *frameBuffer, uint8_t len);

    // Listens for incoming traffic, decrements hop counter, and rebroadcasts
    bool processRelay();

    // Base station: checks for a received frame without blocking. On entry
    // `len` is the size of `buffer` (use RH_RF95_MAX_MESSAGE_LEN); on success
    // it is the frame length and `rssiDbm` is its signal strength. Returns
    // false if nothing arrived or the radio is offline.
    bool receive(uint8_t *buffer, uint8_t &len, int16_t &rssiDbm);

  private:
    float _freqMhz;
    bool _hwReady;
    RH_RF95 _rf95;
};

#endif // LORA_TX_H
