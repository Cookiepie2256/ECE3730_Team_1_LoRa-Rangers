
# Team Project: ECE3730_Team_1_LoRa Rangers

## Project Overview
A fun team project with a search-and-rescue connection

## Team Members
| Name | GitHub Username | Responsibility |
|------|-----------------|----------------|
| [Savannah] | [savannahwalker786] | [DHT11 Sensor] |

## Individual Contribution: [Savannah]

### My Responsibility
DHT11 Sensor humidity% + tempC code

### My Files
- `DHT11Sensor.h` — Defines class and its public functions
- `DHT11Sensor.cpp` — Actual functions

### My Inputs and Outputs
| Direction | Data | Type / Units / Format | Source or Destination |
|-----------|------|-----------------------|-----------------------|
| Input | [Humidity, TempC] | [float, float / %, C /  ] | [DHT11] |
| Output | [Humidity, TempC] | [uint16_t, int16_t / %, C /  ] | [sendData()] |

### My Test Plan
Using ESP32 and basic Arduino set up, I will verify that each sensor reads temperature and RH correctly
```
--------------------------------------------------------------

# Individual Library Documentation

**ECE 3730 | Monitoring System Project | Assignment 1**

Use this report to explain how your software library works, how you tested it, and what your teammates need to know to integrate it. Replace every bracketed prompt with your own information. Remove the prompts before submitting.

| Information | Entry |
|---|---|
| **Student** | [Savannah] |
| **Team** | [LoRa Rangers] |
| **Module** | [DHT11 Temperature and Humidity Sensor] |
| **Date** | [November 9, 2026] |
| **Repository link** | [https://github.com/Cookiepie2256/ECE3730_Team_1_LoRa-Rangers] |

## 1. Library Use and Interface

The DHT11Sensor library measures temperature and relative humidity using a DHT11 sensor connected to an ESP32. The library provides functions to initialize the sensor, retrieve temperature and humidity measurements, and check whether readings are valid.

Temperature is returned as an int16_t in degrees Celsius (°C), while relative humidity is returned as a uint16_t in percent relative humidity (%RH). These data types allow the measurements to be passed to the packet encoding module for eventual LoRa transmission.

The library should be used in the following order:

Create a DHT11Sensor object and assign the sensor's GPIO pin.

Call begin() to initialize the sensor.

Call readSensor() to obtain new measurements and check for valid data.

If the reading is successful, use the getter functions to retrieve temperature and humidity.

| Public function | Inputs | Returns or output | Purpose and use notes |
|---|---|---|---|
| [DHT11Sensor(int pin)] | [GPIO pin] | [Sensor object] | [Constructor that assigns data pin] |
| [begin()] | [None] | [None] | [Initializes sensor] |
| [readSensor()] | [None] | [bool] | [Reads temp and rh, returns true if valid readings] |
| [getTempC()] | [None] | [int16_t/C] | [Returns most recently measured temp] |
| [getHumidity()] | [None] | [uint16_t/%RH] | [Returns most recently measured RH] |

Add or remove rows as needed. Keep function names consistent with your `.h` and `.cpp` files.

### Example of Use by Another Team Member

Paste a short example showing how another sketch should include and call your library. Explain any required setup, pin assignments, or initialization. Keep the example consistent with your test sketch.

```cpp
// #include "DHT11Sensor.h"

#define DHT_PIN 2

DHT11Sensor mySensor(DHT_PIN);

void setup() {
    Serial.begin(9600);
    mySensor.begin();
}

void loop() {

    if (mySensor.readSensor()) {

        int16_t tempC = mySensor.getTempC();
        uint16_t humidity = mySensor.getHumidity();

        Serial.print("Temperature: ");
        Serial.println(tempC);

        Serial.print("Humidity: ");
        Serial.println(humidity);

        // Pass measurements to packet encoding module
        // packData(tempC, humidity);
    }

    delay(30000);
}
```

## 2. Testing and Results

The test sketch creates a DHT11Sensor object, initializes the sensor, and repeatedly calls its public functions to obtain temperature and humidity measurements. The returned values are displayed through the Arduino Serial Monitor to verify that the library is working independently.

| Test item | Description |
|---|---|
| **Test setup** | [ESP32 development board connected to a DHT11 temperature and humidity sensor. The sensor data pin is connected to GPIO 2.] |
| **Test procedure** | [Initialize the sensor using begin(), repeatedly call readSensor(), and retrieve temperature and humidity using the getter functions. Display the measurements through the Serial Monitor.] |
| **Expected result** | [The sensor should produce temperature readings in degrees Celsius and relative humidity readings in %RH. Invalid readings should be detected by the library.] |
| **Observed result** | [The sensor successfully produced temperature and humidity readings through the Serial Monitor. Indoor temperature readings were approximately 20°C (68°F), and indoor humidity readings were approximately 40%RH. Outdoor testing showed changes in humidity, while temperature readings did not change as much as expected. Further testing is needed to verify accuracy under changing environmental conditions.] |

Insert a readable screenshot or photo of the test output. Add a caption identifying what it shows. Do not include unrelated desktop content.

> **[Insert screenshot or photo here]**
>![alt text](<DHT11_test_data.png>)
> *Caption: [Serial Monitor displaying temperature in Celsius and relative humidity measurements obtained from the DHT11 sensor.]*

## 3. Current Status and Integration Notes

Explain what is working, any known problems or limitations, and what your teammates should know before integrating your library.

| Item | Notes |
|---|---|
| **Working** | [Sensor initialization, temperature readings, humidity readings, conversion to int16_t and uint16_t, and validation of sensor readings.] |
| **Known issues** | [RH readings do not work on one of the sensors. The sensors are slow to register changes in temp, and slow to read any sudden drops in RH leve.] |
| **Integration notes** | [Uses an ESP32 with the DHT11 data pin connected to GPIO 2. Temperature is output as int16_t in °C, and humidity is output as uint16_t in %RH. Both values can be passed to the packet encoding module. The main program controls the sampling interval, with approximately 30 seconds being the intended system sampling period. Downloaded the Adafruit DHT sensor library.] |
| **Next step** | [Integrate the temperature and humidity outputs with the packet encoding and LoRa transmission modules. Verify that both measurements are correctly packed, transmitted, and decoded by the base station. Calibrate sensors.] |

## Submission Checklist

- [ ] Completed report with screenshot or photo of test output
- [ ] Library header file (`.h`) and implementation file (`.cpp`)
- [ ] Test sketch (`.ino`) that calls the library’s public functions
- [ ] Short demonstration video showing the library working
- [ ] All files pushed to the team GitHub repository

---

*ECE 3730 | Individual Library Documentation*


