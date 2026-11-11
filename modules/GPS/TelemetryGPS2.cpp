#include "TelemetryGPS2.h"

TelemetryGPS::TelemetryGPS(HardwareSerial &serialPort, uint8_t rxPin,
                           uint8_t txPin, uint32_t baudRate)
    : _serialPort(serialPort), _rxPin(rxPin), _txPin(txPin),
      _baudRate(baudRate) {}

void TelemetryGPS::begin() {
    _serialPort.begin(_baudRate, SERIAL_8N1, _rxPin, _txPin);
}

void TelemetryGPS::update() {
    while (_serialPort.available() > 0) {
        _gps.encode(_serialPort.read());
    }
}

bool TelemetryGPS::hasFix() { return _gps.location.isValid(); }

// 1. THIS IS THE NEW METHOD IMPLEMENTATION
GpsCoordinates TelemetryGPS::getGPS() {
    GpsCoordinates currentCoords;

    // Pack the struct with data from the TinyGPSPlus parser
    currentCoords.latitude = _gps.location.lat();
    currentCoords.longitude = _gps.location.lng();

    // Return the packaged struct back to your main sketch
    return currentCoords;
}

double TelemetryGPS::getLatitude() { return _gps.location.lat(); }

double TelemetryGPS::getLongitude() { return _gps.location.lng(); }

TinyGPSPlus &TelemetryGPS::getRawParser() { return _gps; }
