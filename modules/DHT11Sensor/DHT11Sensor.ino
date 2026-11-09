
#include "DHT11Sensor.h"

// DHT11 connected to digital pin 2 but this can obv be whatever we want
#define DHT_PIN 2

// Create sensor object
DHT11Sensor mySensor(DHT_PIN);

void setup() {
    Serial.begin(9600);

    mySensor.begin();
}

void loop() {

    // Attempt to read sensor
    if (mySensor.readSensor()) {

        // Unpacked data is here ↓
        int16_t tempC = mySensor.getTempC();
        uint16_t humidity = mySensor.getHumidity();

        // for testing
        Serial.print("Temperature: ");
        Serial.print(tempC);
        Serial.println(" C");

        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %RH");

    }
    else {
        Serial.println("DHT11 reading failed!");
    }

    // DHT11 should not be read more frequently than every 1 second
    delay(2000);
}

