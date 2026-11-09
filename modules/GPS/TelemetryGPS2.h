#ifndef TELEMETRY_GPS_H
#define TELEMETRY_GPS_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>

// 1. THIS IS THE MISSING STRUCT
struct GpsCoordinates {
  double latitude;
  double longitude;
};

class TelemetryGPS {
  private:
    HardwareSerial& _serialPort;
    uint8_t _rxPin;
    uint8_t _txPin;
    uint32_t _baudRate;
    TinyGPSPlus _gps; // The parser lives inside your class

  public:
    // Constructor requires the hardware serial port and pins
    TelemetryGPS(HardwareSerial& serialPort, uint8_t rxPin, uint8_t txPin, uint32_t baudRate = 9600);
    
    // Initializes the serial connection
    void begin();
    
    // Feeds incoming serial data to the parser (must be called continuously)
    void update(); 

    // Helper methods to extract clean data
    bool hasFix();
    
    // 2. THIS IS THE MISSING METHOD
    GpsCoordinates getGPS(); 
    
    double getLatitude();
    double getLongitude();
    
    // Optional: Return a reference to the raw TinyGPSPlus object in case 
    // you need advanced data (like altitude or HDOP) in the main sketch
    TinyGPSPlus& getRawParser();
};

#endif