#ifndef LORA_TX_H
#define LORA_TX_H

// LoRaTx: the LoRa radio link for the sensor nodes and the base station.
//
// Drives the Heltec WiFi LoRa 32 V3's on-board SX1262 radio through RadioLib.
// (It used to use RadioHead's RH_RF95, which only supports SX127x chips and
// never started on the V3 boards.)
//
// Radio settings: 125 kHz bandwidth, SF7, coding rate 4/5, sync word 0x12,
// CRC on. Every node and the base station use these, so they all hear each
// other. The radio sits in receive mode whenever it is not transmitting.
//
// Only one LoRaTx may exist per program (it shares one interrupt flag).

#include <Arduino.h>
#include <PacketCodec.h>
#include <RadioLib.h>

class LoRaTx {
  public:
    // Largest frame receive() can return (the SX1262's buffer size)
    static const uint8_t MAX_FRAME_LEN = 255;

    // Defaults are the Heltec V3 pins
    LoRaTx(uint8_t csPin = 8, uint8_t dio1Pin = 14, float freqMhz = 915.0f,
           uint8_t rstPin = 12, uint8_t busyPin = 13);

    // Starts the radio and puts it in receive mode. txPowerDbm is capped at
    // 22 (the SX1262 maximum). cadTimeoutMs is how long transmitFrame() waits
    // for a busy channel to clear before sending anyway.
    bool begin(int8_t txPowerDbm = 20, uint16_t cadTimeoutMs = 500);

    // Transmits a 32-byte frame using CSMA collision avoidance (listens with
    // channel activity detection first). Returns false if the radio is
    // offline or the send failed.
    bool transmitFrame(const uint8_t *frameBuffer, uint8_t len);

    // Listens for incoming traffic, decrements hop counter, and rebroadcasts
    bool processRelay();

    // Base station: checks for a received frame without blocking. On entry
    // `len` is the size of `buffer` (use LoRaTx::MAX_FRAME_LEN); on success
    // it is the frame length and `rssiDbm` is its signal strength. Returns
    // false if nothing arrived, the frame failed its CRC, or the radio is
    // offline.
    bool receive(uint8_t *buffer, uint8_t &len, int16_t &rssiDbm);

  private:
    bool waitForClearChannel();
    void startListening();

    static void IRAM_ATTR onDio1();
    static volatile bool _dio1Flag;

    float _freqMhz;
    uint16_t _cadTimeoutMs;
    bool _hwReady;
    Module _module;
    SX1262 _radio;
};

#endif // LORA_TX_H
