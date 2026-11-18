// LoRa Rangers base station. Board: Heltec WiFi LoRa 32 V3.2.
//
// Plan (node_plans.pdf, part 2):
//   setup(): Serial.begin(), LoRaTx.begin()
//   loop():  1. LoRaTx.receive()  2. PacketCodec.decode()  3. SerialForwarder.forward()
//
// Each packet a node sends is received, checked (length + CRC), decoded and
// sent to the PC as one [CSVDATA] line (record it with `just log <port>`).
// It also goes to the WebDashboard: join WiFi "LoRaRangers-Base" (password
// rangers123) and open http://192.168.4.1. Set USE_WEB_DASHBOARD to 0 to turn
// the dashboard off.
//
// The OLED shows the radio and WiFi status, packet counts and the last packet
// received. "RX!" appears at the top for 2 s each time a packet arrives.
//
// The base station only listens. It never relays packets; the nodes do that.
// A relayed copy of a packet the base already heard directly is still
// forwarded (with fewer hops left); the dashboard counts it as a duplicate.

#define USE_WEB_DASHBOARD 1

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LoRaTx.h>
#include <PacketCodec.h>
#include <SerialForwarder.h>
#include <Wire.h>
#if USE_WEB_DASHBOARD
#include <WebDashboard.h>
#include <WebDashboardCodec.h>
#endif

// ---- Pins (Heltec V3) ----
const uint8_t OLED_SDA = 17;
const uint8_t OLED_SCL = 18;
const uint8_t OLED_RST = 21;
const uint8_t VEXT_PIN = 36;     // LOW powers the OLED
const uint8_t LORA_NSS_PIN = 8;  // On-board SX1262 radio
const uint8_t LORA_DIO1_PIN = 14;
const float LORA_FREQ_MHZ = 915.0f; // Must match the nodes

const uint32_t DISPLAY_REFRESH_MS = 1000; // Redraw for the "seconds ago" count
const uint32_t RX_FLASH_MS = 2000;        // How long "RX!" stays up

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
LoRaTx radio(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_FREQ_MHZ);
SerialForwarder forwarder(Serial);
#if USE_WEB_DASHBOARD
WebDashboard dashboard;
#endif

bool displayOk = false;
bool radioOk = false;
bool wifiOk = false;

uint32_t framesReceived = 0; // Everything the radio heard
uint32_t framesRejected = 0; // Wrong length or bad checksum

// Last good packet, for the screen
bool havePacket = false;
DecodedPacket lastPkt;
int16_t lastRssi = 0;
uint32_t lastRxMs = 0;
const char *lastResult = "";

bool displayDirty = true;
uint32_t lastDrawMs = 0;

// Same ID format as the dashboard: small IDs as-is, MAC-based ones in hex
void printNodeName(Print &out, uint16_t id) {
    if (id > 255) {
        out.printf("Node %04X", (unsigned)id);
    } else {
        out.printf("Node %u", (unsigned)id);
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

void drawScreen() {
    const uint32_t now = millis();
    display.clearDisplay();
    display.setCursor(0, 0);

    // Header: counts, and "RX!" right after a packet
    display.printf("BASE rx %lu bad %lu", (unsigned long)framesReceived,
                   (unsigned long)framesRejected);
    if (havePacket && now - lastRxMs < RX_FLASH_MS) {
        display.setCursor(110, 0);
        display.print("RX!");
    }
    display.setCursor(0, 8);
    display.printf("LoRa:%s WiFi:%s\n", radioOk ? "OK" : "FAIL",
                   wifiOk ? "OK" : "OFF");

    if (!havePacket) {
        display.println();
        display.println(radioOk ? "Listening..." : "No radio, can't RX");
        display.println("No packets yet");
#if USE_WEB_DASHBOARD
        if (wifiOk) {
            display.println();
            display.println(DASH_AP_SSID);
            display.println("http://192.168.4.1");
        }
#endif
        display.display();
        return;
    }

    display.print("Last: ");
    printNodeName(display, lastPkt.nodeId);
    display.printf(" #%u\n", (unsigned)lastPkt.seqNum);
    display.printf("RSSI %d dBm  hops %u\n", (int)lastRssi,
                   (unsigned)lastPkt.hops);
    display.printf("T %.0fC  H %.0f%%\n", (double)lastPkt.tempC,
                   (double)lastPkt.humPct);
    if (lastPkt.latitude == 0.0 && lastPkt.longitude == 0.0) {
        display.println("GPS: no fix");
    } else {
        display.printf("%.4f,%.4f\n", lastPkt.latitude, lastPkt.longitude);
    }
    display.printf("Bat %.2fV\n", lastPkt.batteryMv / 1000.0);
    display.printf("%lus ago %s", (unsigned long)((now - lastRxMs) / 1000),
                   lastResult);
    display.display();
}

// Redraw after a packet, and once a second for the "ago" count. Drawing
// takes a few ms, so not every loop.
void updateDisplay() {
    if (!displayOk) {
        return;
    }
    const uint32_t now = millis();
    if (displayDirty || now - lastDrawMs >= DISPLAY_REFRESH_MS) {
        drawScreen();
        displayDirty = false;
        lastDrawMs = now;
    }
}

// Steps 1-3 of the plan for one frame, if one is waiting
void handleRadio() {
    // 1. Receive: returns at once if no frame is waiting
    uint8_t frame[LoRaTx::MAX_FRAME_LEN];
    uint8_t len = sizeof(frame);
    int16_t rssi = 0;
    if (!radioOk || !radio.receive(frame, len, rssi)) {
        return;
    }
    framesReceived++;
    displayDirty = true;

    // 2. Decode: rejects frames with the wrong length or a bad checksum
    DecodedPacket pkt;
    if (!PacketCodec::decode(frame, len, pkt)) {
        framesRejected++;
        Serial.printf("[RX] Rejected %u-byte frame (bad length or checksum), "
                      "RSSI %d dBm, %lu of %lu rejected\n",
                      (unsigned)len, (int)rssi, (unsigned long)framesRejected,
                      (unsigned long)framesReceived);
        return;
    }

    // 3. Forward to the PC; the node ID identifies who sent it
    forwarder.forward(pkt, rssi);

    havePacket = true;
    lastPkt = pkt;
    lastRssi = rssi;
    lastRxMs = millis();
    lastResult = "";

#if USE_WEB_DASHBOARD
    UpdateResult result =
        dashboard.update(toDashboardReading(pkt, rssi), millis());
    if (result != UpdateResult::NEW) {
        lastResult = WebDashboard::resultName(result); // GAP, DUPLICATE...
    }
    if (result != UpdateResult::NEW && result != UpdateResult::GAP) {
        Serial.printf("[RX] Node %u seq %u: %s\n", (unsigned)pkt.nodeId,
                      (unsigned)pkt.seqNum, WebDashboard::resultName(result));
    }
#endif
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("==================================================");
    Serial.println(" ECE-3730: LoRa Rangers Base Station");
    Serial.println("==================================================");

    // Power up and start the OLED. If it fails the base still works.
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
    showBootStatus(displayOk ? "Base station booting" : "OLED init failed");

    showBootStatus("Starting LoRa...");
    radioOk = radio.begin();
    // Keep running without a radio so the dashboard still works
    showBootStatus(radioOk ? "LoRa: OK, listening" : "LoRa: FAILED");

#if USE_WEB_DASHBOARD
    wifiOk = dashboard.begin();
    if (wifiOk) {
        Serial.printf("[INIT] Dashboard: WiFi \"%s\" (password %s) -> http://%s\n",
                      DASH_AP_SSID, DASH_AP_PASS,
                      WiFi.softAPIP().toString().c_str());
    }
    showBootStatus(wifiOk ? "WiFi: OK" : "WiFi: FAILED");
#endif

    delay(1500); // Leave the boot messages up long enough to read
}

void loop() {
#if USE_WEB_DASHBOARD
    dashboard.handleClient(); // Must run often, so loop() never blocks
#endif
    handleRadio();
    updateDisplay();
}
