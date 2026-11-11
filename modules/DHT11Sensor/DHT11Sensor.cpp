
#include "DHT11Sensor.h"
#include <math.h>

// Constructor
DHT11Sensor::DHT11Sensor(int pin)
    : _pin(pin), _dht(pin, DHT11), _tempC(0), _humidity(0) {}

// Initialize sensor
void DHT11Sensor::begin() { _dht.begin(); }

// Read sensor data
bool DHT11Sensor::readSensor() {

    float temperature = _dht.readTemperature();
    float humidity = _dht.readHumidity();

    // Check for failed readings
    if (isnan(temperature) || isnan(humidity)) {
        return false;
    }

    // Convert to integer data types
    _tempC = static_cast<int16_t>(roundf(temperature));
    _humidity = static_cast<uint16_t>(roundf(humidity));

    return true;
}

// Return temperature in Celsius
int16_t DHT11Sensor::getTempC() const { return _tempC; }

// Return relative humidity
uint16_t DHT11Sensor::getHumidity() const { return _humidity; }
