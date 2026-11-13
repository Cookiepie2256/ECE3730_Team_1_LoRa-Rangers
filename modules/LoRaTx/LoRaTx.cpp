#include "LoRaTx.h"

LoRaTx::LoRaTx(uint8_t csPin, uint8_t intPin, float freqMhz)
    : _freqMhz(freqMhz), _hwReady(false), _rf95(csPin, intPin) {}

bool LoRaTx::begin(int8_t txPowerDbm, uint16_t cadTimeoutMs) {
    if (!_rf95.init()) {
        _hwReady = false;
        return false;
    }

    _rf95.setFrequency(_freqMhz);
    _rf95.setTxPower(txPowerDbm, false);
    _rf95.setCADTimeout(cadTimeoutMs);

    _hwReady = true;
    return true;
}

bool LoRaTx::transmitFrame(const uint8_t *frameBuffer, uint8_t len) {
    if (frameBuffer == nullptr || len != PacketCodec::PACKET_SIZE)
        return false;
    if (!_hwReady)
        return true; // Simulated success if hardware disconnected

    _rf95.waitCAD(); // Listen-Before-Talk
    _rf95.send((uint8_t *)frameBuffer, len);
    _rf95.waitPacketSent();
    return true;
}

bool LoRaTx::receive(uint8_t *buffer, uint8_t &len, int16_t &rssiDbm) {
    if (buffer == nullptr || !_hwReady || !_rf95.available())
        return false;

    uint8_t rxLen = len;
    if (!_rf95.recv(buffer, &rxLen))
        return false;

    len = rxLen;
    rssiDbm = _rf95.lastRssi();
    return true;
}

bool LoRaTx::processRelay() {
    if (!_hwReady || !_rf95.available())
        return false;

    uint8_t rxBuffer[RH_RF95_MAX_MESSAGE_LEN];
    uint8_t rxLen = sizeof(rxBuffer);

    if (_rf95.recv(rxBuffer, &rxLen)) {
        if (rxLen == PacketCodec::PACKET_SIZE) {
            if (PacketCodec::decrementHops(rxBuffer, rxLen)) {
                _rf95.waitCAD();
                _rf95.send(rxBuffer, rxLen);
                _rf95.waitPacketSent();
                return true;
            }
        }
    }
    return false;
}
