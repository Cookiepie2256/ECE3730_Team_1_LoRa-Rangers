
#ifndef DHT11SENSOR_H
#define DHT11SENSOR_H

#include <Arduino.h>
#include <DHT.h> // installed Adafruits DHT sensor library to make life easier
#include <stdint.h>

class DHT11Sensor {
  private:
    const int _pin;
    DHT _dht;

    int16_t _tempC;
    uint16_t _humidity;

  public:
    // Constructor
    DHT11Sensor(int pin);

    // Initialize sensor
    void begin();

    // Read temperature and humidity
    bool readSensor();

    // Getters
    int16_t getTempC() const;
    uint16_t getHumidity() const;
};

#endif
