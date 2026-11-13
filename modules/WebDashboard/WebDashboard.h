#ifndef WEB_DASHBOARD_H
#define WEB_DASHBOARD_H

// WebDashboard: live web GUI for the LoRa Rangers base station.
//
// The base station ESP32 starts its own WiFi access point and serves a page at
// http://192.168.4.1 that shows every node's latest reading, a position map,
// charts and a packet log. No internet is needed.
//
// Feed it decoded packets with update() (a DashboardReading) or
// updateFromCsv() (one SerialForwarder line), and call handleClient() from
// loop(). See README.md, section "Integration".

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

// ---- Configuration (override with #define before including this file) ----
#ifndef DASH_MAX_NODES
#define DASH_MAX_NODES 3 // Node slots shown on the page (see update())
#endif
#ifndef DASH_LATE_MS
#define DASH_LATE_MS 60000UL // No packet for 60 s -> LATE (2 missed sends)
#endif
#ifndef DASH_LOST_MS
#define DASH_LOST_MS 150000UL // No packet for 150 s -> LOST (5 missed sends)
#endif
#ifndef DASH_HISTORY
#define DASH_HISTORY 64 // Accepted packets kept for page refreshes
#endif
#ifndef DASH_AP_SSID
#define DASH_AP_SSID "LoRaRangers-Base"
#endif
#ifndef DASH_AP_PASS
#define DASH_AP_PASS "rangers123" // At least 8 characters (WPA2)
#endif
#ifndef DASH_JSON_BUF
#define DASH_JSON_BUF 16384 // /data buffer (static, not heap); fits all history
#endif
#ifndef DASH_OLD_WINDOW
#define DASH_OLD_WINDOW 64 // A seq up to this far behind the last one is OLD
#endif
#ifndef DASH_MAX_GAP
#define DASH_MAX_GAP 1000 // Bigger forward jumps resync instead of "missed"
#endif
#ifndef DASH_CSV_MAX_LEN
#define DASH_CSV_MAX_LEN 160 // Longest CSV line updateFromCsv() accepts
#endif

// One decoded packet, in real units. Field order matches the SerialForwarder
// line: node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,
// battery_mv,rssi_dbm
struct DashboardReading {
    uint16_t nodeId; // Any ID except 0, e.g. from NodeId::get()
    uint16_t seqNum;
    int32_t elapsedMs; // ms since that node booted (not a shared clock)
    double lat;        // degrees
    double lng;        // degrees
    float tempC;       // degrees Celsius
    float humidity;    // % relative humidity
    uint8_t hopLimit;  // hops left (TTL)
    uint16_t batteryMv;
    int16_t rssi; // dBm, measured by the base station radio
};

enum class UpdateResult : uint8_t { NEW, GAP, DUPLICATE, OLD, BAD_NODE };
enum class NodeStatus : uint8_t { NO_DATA, OK, LATE, LOST };

// Everything the dashboard knows about one node
struct NodeState {
    bool hasData;
    uint16_t nodeId;       // Node ID that owns this slot (0 = slot free)
    DashboardReading last; // Last accepted reading
    UpdateResult lastResult;
    uint32_t receivedAtMs; // Base station millis() when `last` arrived
    uint32_t received;     // Accepted packets (NEW + GAP)
    uint32_t missed;       // Sequence numbers never seen
    uint32_t duplicates;   // Same seq received again (ignored)
    uint32_t oldPackets;   // Older seq arriving late (ignored)
    uint32_t reboots;      // Node restarts detected (tracking was reset)
};

// One entry of the packet history ring buffer
struct DashboardPacket {
    uint32_t uid; // Global update number: 1, 2, 3...
    uint32_t receivedAtMs;
    UpdateResult result;
    uint8_t slot; // 1..DASH_MAX_NODES
    DashboardReading reading;
};

class WebDashboard {
  public:
    // CSV header matching logLine()
    static const char *const LOG_HEADER;

    explicit WebDashboard(uint16_t port = 80);

    // Starts the WiFi access point at 192.168.4.1 and the web server.
    // Returns false if the access point could not start.
    bool begin(const char *ssid = DASH_AP_SSID, const char *pass = DASH_AP_PASS);

    // Serves web requests. Call every loop(). Returns at once when idle;
    // sending the page or a full history takes a few ms.
    void handleClient();

    // Main input: one decoded reading, stamped with the base station's
    // millis() on arrival.
    //
    // Node IDs can be anything except 0. The first time an ID is seen it gets
    // a page slot: slot N if the ID is N (1..DASH_MAX_NODES) and that slot is
    // free, otherwise the first free slot. Returns BAD_NODE for ID 0 or when
    // every slot is taken by other nodes. Slots last until the base restarts.
    UpdateResult update(const DashboardReading &reading, uint32_t nowMs);

    // Parses one SerialForwarder line (the "[CSVDATA] " prefix is optional)
    // and passes it to update(). Returns false if the line is malformed or
    // update() rejects the node ID (then *out is BAD_NODE).
    bool updateFromCsv(const char *line, uint32_t nowMs,
                       UpdateResult *out = nullptr);

    NodeStatus getStatus(uint16_t nodeId, uint32_t nowMs) const;
    void setTimeouts(uint32_t lateMs, uint32_t lostMs);

    // Optional label shown in the page header, e.g. "Presentation demo".
    // Letters, digits, spaces and simple punctuation only (no quotes).
    // nullptr (the default) shows nothing.
    void setModeLabel(const char *label) { modeLabel_ = label; }

    // nullptr if this node ID hasn't been seen
    const NodeState *getNode(uint16_t nodeId) const;

    // Page slot (1..DASH_MAX_NODES) given to a node ID, or 0 if none
    uint8_t slotOf(uint16_t nodeId) const;

    // Total accepted packets so far (also the newest uid)
    uint32_t lastUid() const { return nextUid_ - 1; }

    // Writes the /data JSON into buf. Only packets with uid > sinceId are
    // included. If they don't all fit, the newest ones are left out, "more"
    // is true and "lastId" tells the page where to continue. Output is always
    // complete JSON or empty. Returns bytes written (0 if buf is too small).
    size_t buildJson(char *buf, size_t len, uint32_t nowMs,
                     uint32_t sinceId) const;

    // One CSV line (see LOG_HEADER) describing the node's latest reading,
    // for Serial or an SD card. Returns 0 if there's no data or no room.
    size_t logLine(uint16_t nodeId, char *buf, size_t len) const;

    static const char *resultName(UpdateResult r);
    static const char *statusName(NodeStatus s);

  private:
    NodeState nodes_[DASH_MAX_NODES];
    DashboardPacket history_[DASH_HISTORY]; // Slot for uid u: (u - 1) % size
    uint32_t nextUid_;
    uint32_t lateMs_;
    uint32_t lostMs_;
    const char *modeLabel_;
    uint32_t bootId_; // Random per power-up, so the page spots a restart
    bool started_;
    WebServer server_;

    uint8_t claimSlot(uint16_t nodeId);
    NodeStatus statusOf(const NodeState &st, uint32_t nowMs) const;
    void accept(uint8_t slot, const DashboardReading &r, UpdateResult result,
                uint32_t nowMs);
    void handleRoot();
    void handleData();
    void handleNotFound();
};

#endif // WEB_DASHBOARD_H
