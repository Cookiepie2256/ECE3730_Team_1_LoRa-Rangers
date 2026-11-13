#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>          // Install via Arduino Library Manager

// Your custom libraries
#include "TelemetryGPS2.h"
#include "DHT11Sensor.h"
#include "PacketCodec.h"

// --- Hardware Pin Definitions ---

// OLED
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_PIN 36 // OLED Power Control

// LoRa SX1262 (Heltec V3 specific internal wiring)
#define LORA_NSS 8
#define LORA_DIO1 14
#define LORA_NRST 12
#define LORA_BUSY 13

// GPS (Moved away from OLED pins!)
#define GPS_RX_PIN 47
#define GPS_TX_PIN 48

// DHT11
#define DHT_PIN 2

// Battery Monitoring (Heltec V3 specific)
#define BAT_ADC_PIN 1
#define BAT_CTRL_PIN 37 // Must be pulled LOW to read battery

// --- Object Instantiation ---

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);
TelemetryGPS myGps(Serial2, GPS_RX_PIN, GPS_TX_PIN, 9600);
DHT11Sensor mySensor(DHT_PIN);

// --- Telemetry State ---
const uint16_t THIS_NODE_ID = 1;
uint16_t currentSequence = 1;
unsigned long lastTxTime = 0;
const unsigned long TX_INTERVAL = 15000; // 15 seconds

// Helper function to read the internal battery voltage on the Heltec V3
uint16_t getBatteryMillivolts() {
    digitalWrite(BAT_CTRL_PIN, LOW); // Turn on the voltage divider
    delay(10);                       // Allow voltage to stabilize
    int adcVal = analogRead(BAT_ADC_PIN);
    digitalWrite(BAT_CTRL_PIN, HIGH); // Turn it back off to save power
    
    // V3 uses a 390k/100k voltage divider. 
    // 3.3V reference / 4095 (12-bit ADC) * 4.9 (divider ratio) * 1000 = ~3.948 multiplier
    return (uint16_t)(adcVal * 3.948); 
}

void setup() {
    Serial.begin(115200);

    // 1. Power up the OLED
    pinMode(VEXT_PIN, OUTPUT);
    digitalWrite(VEXT_PIN, LOW);
    delay(50);

    // 2. Initialize OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("OLED init failed"));
        while(true);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("System Booting...");
    display.display();

    // 3. Initialize Battery Control Pin
    pinMode(BAT_CTRL_PIN, OUTPUT);
    digitalWrite(BAT_CTRL_PIN, HIGH); // Keep divider off by default
    analogReadResolution(12);         // Ensure 12-bit ADC for battery math

    // 4. Initialize Sensors
    myGps.begin();
    mySensor.begin();

    // 5. Initialize LoRa Radio
    display.println("Starting LoRa...");
    display.display();
    
    // Set frequency to 915.0 MHz (US frequency block for your MakerHawk antenna)
    // 125 kHz bandwidth, SF 7, CR 4/5, Sync Word 0x12, Power 10 dBm
    int state = radio.begin(915.0, 125.0, 7, 5, 0x12, 10, 8, 1.6, false);
    
    if (state == RADIOLIB_ERR_NONE) {
        display.println("LoRa: OK");
    } else {
        display.printf("LoRa FAIL: %d\n", state);
        while(true); // Halt if radio fails
    }
    display.display();
    delay(2000);
}

void loop() {
    // Keep feeding the GPS parser constantly
    myGps.update();

    if (millis() - lastTxTime >= TX_INTERVAL) {
        lastTxTime = millis();

        // --- 1. Gather Sensor Data ---
        
        mySensor.readSensor();
        float temp = mySensor.getTempC();
        float hum = mySensor.getHumidity();

        double lat = 0.0;
        double lon = 0.0;
        bool fix = myGps.hasFix();
        if (fix) {
            GpsCoordinates coords = myGps.getGPS();
            lat = coords.latitude;
            lon = coords.longitude;
        }

        uint16_t batteryMv = getBatteryMillivolts();

        // --- 2. Encode Payload using PacketCodec ---
        
        uint8_t txBuffer[PacketCodec::PACKET_SIZE];
        uint8_t written = PacketCodec::encode(
            THIS_NODE_ID, currentSequence, millis(), 
            lon, lat, temp, hum, 3, batteryMv, txBuffer
        );

        // --- 3. Transmit via LoRa ---
        
        int txState = radio.transmit(txBuffer, written);

        // --- 4. Update the OLED Screen ---
        
        display.clearDisplay();
        display.setCursor(0, 0);
        
        // Header
        display.printf("Node %d | Seq %d\n", THIS_NODE_ID, currentSequence);
        
        // TX Status
        if(txState == RADIOLIB_ERR_NONE) {
            display.println("TX: SUCCESS");
        } else {
            display.printf("TX: ERROR %d\n", txState);
        }

        // Sensor Data
        display.printf("T: %.1fC  H: %.1f%%\n", temp, hum);
        
        // GPS Status
        if(fix) {
            display.printf("GPS:%.3f,%.3f\n", lat, lon);
        } else {
            display.println("GPS: Searching...");
        }
        
        // Battery
        display.printf("Bat: %u mV\n", batteryMv);
        
        display.display();

        // Increment sequence for the next packet
        currentSequence++;
    }
}