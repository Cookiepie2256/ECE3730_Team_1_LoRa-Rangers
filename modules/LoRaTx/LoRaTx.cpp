#include "LoRaTx.h"

namespace {
const float BANDWIDTH_KHZ = 125.0f;
const uint8_t SPREADING_FACTOR = 7;
const uint8_t CODING_RATE = 5; // 4/5
const uint8_t SYNC_WORD = 0x12;
const uint16_t PREAMBLE_LEN = 8;
const float TCXO_VOLTAGE = 1.8f; // Heltec V3 radio clock
const float CURRENT_LIMIT_MA = 140.0f;
const int8_t MAX_TX_POWER_DBM = 22;
} // namespace

volatile bool LoRaTx::_dio1Flag = false;

void IRAM_ATTR LoRaTx::onDio1() { _dio1Flag = true; }

LoRaTx::LoRaTx(uint8_t csPin, uint8_t dio1Pin, float freqMhz, uint8_t rstPin,
               uint8_t busyPin)
    : _freqMhz(freqMhz), _cadTimeoutMs(500), _hwReady(false),
      _module(csPin, dio1Pin, rstPin, busyPin), _radio(&_module) {}

bool LoRaTx::begin(int8_t txPowerDbm, uint16_t cadTimeoutMs) {
    _hwReady = false;
    _cadTimeoutMs = cadTimeoutMs;
    if (txPowerDbm > MAX_TX_POWER_DBM) {
        txPowerDbm = MAX_TX_POWER_DBM;
    }

    int16_t state =
        _radio.begin(_freqMhz, BANDWIDTH_KHZ, SPREADING_FACTOR, CODING_RATE,
                     SYNC_WORD, txPowerDbm, PREAMBLE_LEN, TCXO_VOLTAGE);
    if (state != RADIOLIB_ERR_NONE) {
        return false;
    }
    _radio.setCurrentLimit(CURRENT_LIMIT_MA);
    _radio.setCRC(true);
    _radio.setDio1Action(onDio1);

    _hwReady = true;
    startListening();
    return true;
}

void LoRaTx::startListening() {
    _dio1Flag = false;
    _radio.startReceive(); // Continuous receive; DIO1 rises when a frame lands
}

// Listen-before-talk: run channel activity detection until the channel is
// free, backing off a random few ms each time it is busy. Gives up (and
// sends anyway) after cadTimeoutMs so a stuck channel can't block forever.
bool LoRaTx::waitForClearChannel() {
    const uint32_t start = millis();
    while (true) {
        const int16_t state = _radio.scanChannel();
        if (state == RADIOLIB_CHANNEL_FREE) {
            return true;
        }
        if (state != RADIOLIB_LORA_DETECTED) {
            return false; // Scan error: just send
        }
        if (millis() - start >= _cadTimeoutMs) {
            return false;
        }
        delay(random(5, 30));
    }
}

bool LoRaTx::transmitFrame(const uint8_t *frameBuffer, uint8_t len) {
    if (frameBuffer == nullptr || len != PacketCodec::PACKET_SIZE)
        return false;
    if (!_hwReady)
        return false;

    waitForClearChannel();
    const int16_t state = _radio.transmit(const_cast<uint8_t *>(frameBuffer), len);

    startListening(); // transmit() and scanChannel() also set DIO1; reset it
    return state == RADIOLIB_ERR_NONE;
}

bool LoRaTx::receive(uint8_t *buffer, uint8_t &len, int16_t &rssiDbm) {
    if (buffer == nullptr || !_hwReady || !_dio1Flag)
        return false;
    _dio1Flag = false;

    const size_t rxLen = _radio.getPacketLength();
    const int16_t state = _radio.readData(buffer, len);
    rssiDbm = (int16_t)_radio.getRSSI();
    startListening();

    if (state != RADIOLIB_ERR_NONE || rxLen == 0 || rxLen > len)
        return false;

    len = (uint8_t)rxLen;
    return true;
}

bool LoRaTx::processRelay() {
    uint8_t rxBuffer[MAX_FRAME_LEN];
    uint8_t rxLen = sizeof(rxBuffer);
    int16_t rssi = 0;

    if (!receive(rxBuffer, rxLen, rssi))
        return false;
    if (rxLen != PacketCodec::PACKET_SIZE)
        return false;
    if (!PacketCodec::decrementHops(rxBuffer, rxLen))
        return false;

    return transmitFrame(rxBuffer, rxLen);
}
