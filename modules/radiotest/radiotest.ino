#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h> // Ensure v6.6.2 is installed

// --- OLED Pins ---
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21
#define VEXT_PIN 36 // OLED Power Control

// --- LoRa SX1262 Pins (Heltec V3) ---
#define LORA_NSS 8
#define LORA_DIO1 14
#define LORA_NRST 12
#define LORA_BUSY 13

// --- Objects ---
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

int packetCount = 0;

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
    display.println("Radio Test Boot...");
    display.display();
    delay(1000);

    // 3. Initialize LoRa Radio (RadioLib v6.6.2 syntax)
    display.println("Starting LoRa...");
    display.display();
    
    // 915 MHz, 125 kHz BW, SF 7, CR 4/5, Sync 0x12, 10 dBm power
    int state = radio.begin(915.0, 125.0, 7, 5, 0x12, 10, 8, 1.6, false);
    
    if (state == RADIOLIB_ERR_NONE) {
        display.println("LoRa Init: SUCCESS");
    } else {
        display.printf("LoRa Init: FAIL (%d)\n", state);
        while(true); // Halt if radio fails to initialize
    }
    display.display();
    delay(2000);
}

void loop() {
    // 1. Create a simple test message
    String message = "Test Packet #" + String(packetCount);
    
    // 2. Transmit the message
    int txState = radio.transmit(message);
    
    // 3. Update the OLED Screen with the result
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("--- LoRa TX Test ---");
    display.println();
    display.println("Sending:");
    display.println(message);
    display.println();
    
    if (txState == RADIOLIB_ERR_NONE) {
        display.println("Status: SUCCESS");
    } else {
        display.printf("Status: ERROR %d\n", txState);
    }
    
    display.display();
    
    // Print to Serial monitor as well
    Serial.print("Transmitting: ");
    Serial.print(message);
    Serial.print(" | State: ");
    Serial.println(txState);
    
    packetCount++;
    delay(3000); // Wait 3 seconds before sending the next packet
}