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

  private:
    float _freqMhz;
    bool _hwReady;
    RH_RF95 _rf95;
};

#endif // LORA_TX_H
