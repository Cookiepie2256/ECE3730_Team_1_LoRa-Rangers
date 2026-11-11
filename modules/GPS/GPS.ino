#include <TelemetryGPS2.h> // Updated to the V2 filename to bypass the IDE cache

#define GPS_RX_PIN 16 
#define GPS_TX_PIN 17 
#define PPS_PIN 18    

// Instantiate the GPS object using UART2
TelemetryGPS myGps(Serial2, GPS_RX_PIN, GPS_TX_PIN, 9600);

unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL = 15000; // 15 seconds

volatile unsigned long lastPpsMicros = 0;
volatile bool ppsTriggered = false;

void IRAM_ATTR onPpsPulse() {
  lastPpsMicros = micros();
  ppsTriggered = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {;}

  myGps.begin();
  
  pinMode(PPS_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PPS_PIN), onPpsPulse, RISING);
  
  Serial.println("Custom GPS Library Test Started...");
}

void loop() {
  // 1. Constantly feed data to the parser
  myGps.update();

  // 2. The 15-second timer
  if (millis() - lastPrintTime >= PRINT_INTERVAL) {
    lastPrintTime = millis();
    
    if (myGps.hasFix()) {
      // 3. Call your new method to get the struct package
      GpsCoordinates myLocation = myGps.getGPS();
      
      // 4. Unpack the struct values
      Serial.print("Map Coordinates: ");
      Serial.print(myLocation.latitude, 6); 
      Serial.print(", ");
      Serial.println(myLocation.longitude, 6);
    } else {
      Serial.println("Waiting for lock...");
    }
  }

  // 5. Handle the microsecond PPS timing pulse
  if (ppsTriggered) {
    ppsTriggered = false;
  }
}
