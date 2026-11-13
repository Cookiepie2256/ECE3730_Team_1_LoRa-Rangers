// WebDashboard example for the LoRa Rangers base station.
//
// 1. Flash this sketch to any ESP32.
// 2. On a laptop or phone, join WiFi "LoRaRangers-Base" (password rangers123).
// 3. Open http://192.168.4.1
//
// DASH_DEMO 2: class presentation. 3 fake rescuers, 6x faster, scripted
//              (GAP 0:35, duplicate 0:55, Node 3 LOST ~1:35, back 1:55,
//              repeats every 2:30). See DEMO.md for the talk track.
// DASH_DEMO 1: the same fake rescuers at real field timing (30 s packets,
//              LATE 60 s, LOST 150 s).
// DASH_DEMO 0: real packets; see "PLUG THE DECODER IN HERE" below.
//
// In every mode you can type or paste a SerialForwarder line into the Serial
// Monitor (115200 baud, "Newline"), e.g.
//   [CSVDATA] 2,17,512340,37.204512,-113.640231,25.4,31.0,3,3950,-92
// and the sketch prints how the dashboard classified it.

#define DASH_DEMO 2

#include "WebDashboard.h"
#if DASH_DEMO
#include "WebDashboardDemo.h"
#endif

WebDashboard dashboard;
#if DASH_DEMO == 2
DashboardDemo demo(DEMO_PRESENTATION);
#elif DASH_DEMO
DashboardDemo demo(DEMO_REALISTIC);
#endif

char serialLine[DASH_CSV_MAX_LEN + 1];
size_t serialLen = 0;
bool serialTooLong = false;

// Prints the dashboard's CSV log line for one node (see LOG_HEADER)
void printLog(uint16_t nodeId) {
    char buf[200];
    if (dashboard.logLine(nodeId, buf, sizeof(buf)) > 0) {
        Serial.println(buf);
    }
}

void onPacket(const DashboardReading &r, UpdateResult result) {
    if (result == UpdateResult::NEW || result == UpdateResult::GAP) {
        printLog(r.nodeId);
    } else {
        Serial.printf("[DASH] node %u seq %u ignored: %s\n", (unsigned)r.nodeId,
                      (unsigned)r.seqNum, WebDashboard::resultName(result));
    }
}

// Node ID at the start of a CSV line (after an optional "[CSVDATA]" prefix)
uint16_t csvNodeId(const char *line) {
    while (*line == ' ') {
        line++;
    }
    if (strncmp(line, "[CSVDATA]", 9) == 0) {
        line += 9;
    }
    return (uint16_t)atoi(line);
}

void handleSerialLine(const char *line) {
    UpdateResult result = UpdateResult::NEW;
    bool ok = dashboard.updateFromCsv(line, millis(), &result);
    if (ok) {
        Serial.printf("[DASH] CSV accepted: %s\n",
                      WebDashboard::resultName(result));
        if (result == UpdateResult::NEW || result == UpdateResult::GAP) {
            printLog(csvNodeId(line));
        }
    } else if (result == UpdateResult::BAD_NODE) {
        Serial.printf("[DASH] CSV rejected: node ID is 0 or all %d node "
                      "slots are taken\n",
                      DASH_MAX_NODES);
    } else {
        Serial.println("[DASH] CSV rejected: expected node_id,seq,elapsed_ms,"
                       "latitude,longitude,temp_c,humidity_pct,hops,"
                       "battery_mv,rssi_dbm");
    }
}

// Collects Serial Monitor input without blocking
void readSerial() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialTooLong) {
                Serial.println("[DASH] CSV rejected: line too long");
            } else if (serialLen > 0) {
                serialLine[serialLen] = '\0';
                handleSerialLine(serialLine);
            }
            serialLen = 0;
            serialTooLong = false;
        } else if (serialLen < DASH_CSV_MAX_LEN) {
            serialLine[serialLen++] = c;
        } else {
            serialTooLong = true;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println();
    Serial.println("==================================================");
    Serial.println(" ECE-3730: WebDashboard (LoRa Rangers base station)");
    Serial.println("==================================================");

#if DASH_DEMO == 2
    dashboard.setTimeouts(DEMO_PRESENT_LATE_MS, DEMO_PRESENT_LOST_MS);
    dashboard.setModeLabel("Presentation demo");
#elif DASH_DEMO
    dashboard.setModeLabel("Demo data");
#endif
    if (dashboard.begin()) {
        Serial.printf("[DASH] WiFi \"%s\" (password %s) -> http://%s\n",
                      DASH_AP_SSID, DASH_AP_PASS,
                      WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[DASH] ERROR: could not start the WiFi access point");
    }
#if DASH_DEMO == 2
    Serial.println("[DASH] Presentation demo: GAP 0:35, duplicate 0:55, "
                   "Node 3 LOST ~1:35, back 1:55, repeats every 2:30");
#elif DASH_DEMO
    Serial.println("[DASH] Demo mode: 3 fake nodes, first packets in ~2 s");
#endif
    Serial.println(WebDashboard::LOG_HEADER);
}

void loop() {
    dashboard.handleClient();
    readSerial();

#if DASH_DEMO
    demo.run(dashboard, millis(), onPacket);
#else
    // ---- PLUG THE DECODER IN HERE ----------------------------------------
    // The base station's receive code (radio + Braxton's decoder) needs one
    // extra line. With PacketCodec linked (just link-libs):
    //
    //   #include <PacketCodec.h>
    //   #include "WebDashboardCodec.h"
    //
    //   uint8_t rx[PacketCodec::PACKET_SIZE];
    //   uint8_t len = sizeof(rx);
    //   if (radio.recv(rx, &len)) {                    // RH_RF95
    //       DecodedPacket pkt;
    //       if (PacketCodec::decode(rx, len, pkt)) {
    //           int16_t rssi = radio.lastRssi();
    //           forwarder.forward(pkt, rssi);          // PC CSV log
    //           UpdateResult r = dashboard.update(toDashboardReading(pkt, rssi),
    //                                             millis());   // <- this line
    //           onPacket(toDashboardReading(pkt, rssi), r);    // optional print
    //       }
    //   }
    //
    // Or, if you already have the text line:
    //   dashboard.updateFromCsv(line, millis());
    // ------------------------------------------------------------------------
#endif
}
