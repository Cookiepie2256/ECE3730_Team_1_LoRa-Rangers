// LoRa Rangers sensor node (one per rescuer). Board: Heltec WiFi LoRa 32 V3.2.
//
// Plan (node_plans.pdf, part 1):
//   setup(): Serial.begin(115200), myGps.begin(), mySensor.begin(), LoRaTx.begin()
//   loop():  1. myGps.getGPS()  2. mySensor.getTempC()  3. mySensor.getHumidity()
//            4. PacketCodec::encode()  5. LoRaTx.transmitFrame()
// After each send, the readings are shown on the board's OLED screen
// (Anthony's displays_sensor_data.ino).
//
// Every board runs this same program. The node ID comes from the ESP32's MAC
// address (NodeId::get()), so nothing has to be changed per board.
//
// loop() never blocks: the GPS needs its serial data read all the time, and
// the node relays other nodes' packets between its own sends.

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT11Sensor.h>
#include <LoRaTx.h>
#include <NodeId.h>
#include <PacketCodec.h>
#include <TelemetryGPS2.h>
#include <Wire.h>

// ---- Pins (Heltec V3) ----
const uint8_t OLED_SDA = 17;
const uint8_t OLED_SCL = 18;
const uint8_t OLED_RST = 21;
const uint8_t VEXT_PIN = 36;  // LOW powers the OLED
const uint8_t BAT_ADC_PIN = 1;
const uint8_t BAT_CTRL_PIN = 37;  // HIGH connects the battery divider (V3.2)
const uint8_t GPS_RX_PIN = 47;    // Kept clear of the OLED pins
const uint8_t GPS_TX_PIN = 48;
const uint8_t DHT_PIN = 2;
const uint8_t LORA_NSS_PIN = 8;  // On-board SX1262 radio
const uint8_t LORA_DIO1_PIN = 14;
const float LORA_FREQ_MHZ = 915.0f;  // Must match the base station

// ---- When to send ----
// The dashboard marks a node LATE after 60 s and LOST after 150 s, so 30 s
// means LATE after 2 missed packets. A random extra delay keeps nodes that
// powered on together from transmitting at the same moment every time.
const uint32_t SEND_INTERVAL_MS = 3000;
const uint32_t SEND_JITTER_MS = 1000;
const uint8_t START_HOPS = 3;  // How many times relays may pass a packet on
const bool ENABLE_RELAY = true;

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
TelemetryGPS myGps(Serial2, GPS_RX_PIN, GPS_TX_PIN, 9600);
DHT11Sensor mySensor(DHT_PIN);
LoRaTx radio(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_FREQ_MHZ);

bool displayOk = false;
bool radioOk = false;
uint16_t nodeId = 0;
uint16_t seqNum = 0;
uint32_t nextSendMs = 0;
uint32_t relayed = 0;

// Last good sensor reading, reused if the DHT11 misses a read
float lastTempC = 0.0f;
float lastHumPct = 0.0f;

// Battery voltage through the board's 390k/100k divider (x4.9)
uint16_t readBatteryMv() {
  digitalWrite(BAT_CTRL_PIN, HIGH);                          // Connect the divider
  delay(10);                                                 // Let the voltage settle
  const uint32_t pinMv = analogReadMilliVolts(BAT_ADC_PIN);  // Calibrated
  digitalWrite(BAT_CTRL_PIN, LOW);                           // Disconnect it again to save power
  return (uint16_t)(pinMv * 49 / 10);
}

// Same ID format as the dashboard: small IDs as-is, MAC-based ones in hex
void printNodeName(Print &out) {
  if (nodeId > 255) {
    out.printf("Node %04X", (unsigned)nodeId);
  } else {
    out.printf("Node %u", (unsigned)nodeId);
  }
}

void showBootStatus(const char *line) {
  Serial.println(line);
  if (!displayOk) {
    return;
  }
  display.println(line);
  display.display();
}

// Anthony's screen layout: header, TX result, sensors, GPS, battery
void showReadings(bool sent, bool sensorOk, bool fix, double lat, double lng,
                  uint16_t batteryMv) {
  if (!displayOk) {
    return;
  }
  display.clearDisplay();
  display.setCursor(0, 0);

  printNodeName(display);
  display.printf(" | Seq %u\n", (unsigned)seqNum);

  if (!radioOk) {
    display.println("TX: NO RADIO");
  } else if (sent) {
    display.println("TX: SUCCESS");
  } else {
    display.println("TX: ERROR");
  }

  display.printf("T: %.0fC  H: %.0f%%%s\n", (double)lastTempC,
                 (double)lastHumPct, sensorOk ? "" : " (old)");

  if (fix) {
    display.printf("GPS:%.4f,%.4f\n", lat, lng);
  } else {
    display.println("GPS: Searching...");
  }

  display.printf("Bat: %u mV\n", (unsigned)batteryMv);
  display.printf("Relayed: %lu\n", (unsigned long)relayed);
  display.display();
}

void scheduleNextSend() {
  nextSendMs = millis() + SEND_INTERVAL_MS + (uint32_t)random(SEND_JITTER_MS);
}

void sendPacket() {
  // 1. GPS position. Without a fix, send 0,0 (the dashboard shows "No GPS fix")
  double lat = 0.0;
  double lng = 0.0;
  const bool fix = myGps.hasFix();
  if (fix) {
    GpsCoordinates pos = myGps.getGPS();
    lat = pos.latitude;
    lng = pos.longitude;
  }

  // 2 + 3. Temperature and humidity
  const bool sensorOk = mySensor.readSensor();
  if (sensorOk) {
    lastTempC = mySensor.getTempC();
    lastHumPct = mySensor.getHumidity();
  }

  const uint16_t batteryMv = readBatteryMv();

  // 4. Pack everything into the 32-byte frame
  seqNum++;
  uint8_t frame[PacketCodec::PACKET_SIZE];
  const uint8_t len = PacketCodec::encode(
    nodeId, seqNum, (int32_t)millis(), lng, lat, lastTempC, lastHumPct,
    START_HOPS, batteryMv, frame);

  // 5. Send it (waits for the channel to be clear first)
  if (!radioOk) {
    display.println("Starting Radio");
    radioOk = radio.begin(20, 500);
  }

  const bool sent = radioOk && radio.transmitFrame(frame, len);

  printNodeName(Serial);
  Serial.printf(" seq %u: %s | %s %.6f, %.6f | %.0f C %.0f %% | %u mV%s\n",
                (unsigned)seqNum, sent ? "sent" : "NOT SENT",
                fix ? "GPS" : "no fix", lat, lng, (double)lastTempC,
                (double)lastHumPct, (unsigned)batteryMv,
                sensorOk ? "" : " (DHT11 read failed, last value)");

  display.display();
  showReadings(sent, sensorOk, fix, lat, lng, batteryMv);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("==================================================");
  Serial.println(" ECE-3730: LoRa Rangers Sensor Node");
  Serial.println("==================================================");

  // Power up and start the OLED. If it fails the node still sends packets.
  pinMode(VEXT_PIN, OUTPUT);
  digitalWrite(VEXT_PIN, LOW);
  delay(50);
  Wire.begin(OLED_SDA, OLED_SCL);
  displayOk = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (displayOk) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
  }
  showBootStatus(displayOk ? "System Booting..." : "OLED init failed");

  nodeId = NodeId::get();
  char line[32];
  snprintf(line, sizeof(line), nodeId > 255 ? "Node ID %04X" : "Node ID %u",
           (unsigned)nodeId);
  showBootStatus(line);

  pinMode(BAT_CTRL_PIN, OUTPUT);
  digitalWrite(BAT_CTRL_PIN, LOW);  // Divider off until a reading
  analogReadResolution(12);

  myGps.begin();
  mySensor.begin();

  showBootStatus("Starting LoRa...");
  radioOk = radio.begin();

  // Keep running without a radio so the screen still shows the sensors
  showBootStatus(radioOk ? "LoRa: OK" : "LoRa: FAILED");

  // Different on every board, so each node's send times drift differently
  randomSeed(esp_random() ^ nodeId);

  // First packet a few seconds after power-up so the boot screen can be read
  nextSendMs = millis() + 3000 + (uint32_t)random(SEND_JITTER_MS);
}

void loop() {
  myGps.update();  // Read GPS serial data as it arrives

  if (ENABLE_RELAY && radioOk && radio.processRelay()) {
    relayed++;
    Serial.println("[RELAY] Passed on another node's packet.");
  }

  // Signed difference so this still works when millis() wraps (~49 days)
  if ((int32_t)(millis() - nextSendMs) >= 0) {
    sendPacket();
    scheduleNextSend();
  }
}
