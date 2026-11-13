#ifndef WEB_DASHBOARD_DEMO_H
#define WEB_DASHBOARD_DEMO_H

// Fake LoRa traffic for testing and presenting the dashboard with no sensor
// nodes. tools/preview_server.py runs the same two scenarios on a laptop.
//
// Realistic (DASH_DEMO 1), real field timing, repeats every 10 minutes:
//   - 3 rescuers walking near Snow Canyon, Utah, one packet each ~30 s +/- 4 s
//   - about 1 in 12 packets lost on air (the next one shows as GAP)
//   - Node 2's packet at minute 2:00 arrives twice (DUPLICATE)
//   - Node 3 out of range from 4:00 to 7:00: LATE (60 s), LOST (150 s), back
//     with a GAP
//
// Presentation (DASH_DEMO 2), everything 6x faster and fully scripted,
// repeats every 2:30 (times from power-on):
//   0:35  Node 1 loses one packet  -> GAP in the log
//   0:55  Node 2 heard twice       -> duplicate counter
//   1:10  Node 3 goes out of range -> LATE ~1:20, LOST + red alert ~1:35
//   1:55  Node 3 back              -> GAP of ~9 missed packets
//   Use with dashboard.setTimeouts(DEMO_PRESENT_LATE_MS, DEMO_PRESENT_LOST_MS)

#include "WebDashboard.h"
#include <math.h>

#define DEMO_PRESENT_LATE_MS 10000UL
#define DEMO_PRESENT_LOST_MS 25000UL

struct DemoScenario {
    uint32_t sendMs;       // time between packets from one node
    long jitterMs;         // +/- random spread (CSMA/CA backoff)
    long dropOneIn;        // random loss, 1 in N packets (0 = none)
    uint32_t cycleMs;      // the scenario repeats with this period
    uint32_t silentFromMs; // Node 3 out of range from this cycle time...
    uint32_t silentToMs;   // ...until this one
    uint32_t gapAtMs;      // Node 1's first packet after this is lost
    uint32_t dupAtMs;      // Node 2's first packet after this arrives twice
    float stepMinM;        // walking distance per packet, meters
    float stepMaxM;
};

static const DemoScenario DEMO_REALISTIC = {
    30000, 4000, 12, 600000, 240000, 420000, 0xFFFFFFFFUL, 120000, 15, 40};
static const DemoScenario DEMO_PRESENTATION = {
    5000, 800, 0, 150000, 70000, 115000, 35000, 55000, 6, 14};

class DashboardDemo {
  public:
    // Called for every packet handed to the dashboard
    typedef void (*PacketCallback)(const DashboardReading &r,
                                   UpdateResult result);

    explicit DashboardDemo(const DemoScenario &scenario = DEMO_REALISTIC)
        : sc_(scenario), dupDueMs_(0), dupPending_(false),
          gapCycle_(0xFFFFFFFFUL), dupCycle_(0xFFFFFFFFUL) {
        const double start[3][3] = {{37.2012, -113.6431, 45},
                                    {37.1987, -113.6385, 0},
                                    {37.2030, -113.6370, 270}};
        const float temp[3] = {24.0f, 27.0f, 22.0f};
        const float hum[3] = {30.0f, 24.0f, 36.0f};
        const int batt[3] = {4150, 4050, 3900};
        for (uint16_t i = 0; i < kNodes; i++) {
            Node &n = nodes_[i];
            n.id = i + 1;
            n.lat = start[i][0];
            n.lng = start[i][1];
            n.heading = start[i][2];
            n.seq = 0;
            n.bootOffsetMs = (i + 1) * 45000UL; // node booted before the base
            n.nextMs = 2000UL + (sc_.sendMs / 10) * i;
            n.batteryMv = batt[i];
            n.tempBase = temp[i];
            n.humBase = hum[i];
        }
    }

    // Call every loop(). Sends every fake packet that is due.
    void run(WebDashboard &dash, uint32_t nowMs, PacketCallback cb) {
        for (uint16_t i = 0; i < kNodes; i++) {
            Node &n = nodes_[i];
            while ((int32_t)(nowMs - n.nextMs) >= 0) {
                const uint32_t sentAt = n.nextMs;
                const uint32_t cycle = sentAt / sc_.cycleMs;
                const uint32_t inCycle = sentAt % sc_.cycleMs;
                n.nextMs += sc_.sendMs + random(-sc_.jitterMs, sc_.jitterMs + 1);
                walk(n);
                n.seq++;
                if (n.id == 3 && inCycle >= sc_.silentFromMs &&
                    inCycle < sc_.silentToMs) {
                    continue; // out of range: sent but never heard
                }
                if (n.id == 1 && inCycle >= sc_.gapAtMs && gapCycle_ != cycle) {
                    gapCycle_ = cycle;
                    continue; // scripted loss: next packet shows a GAP
                }
                if (sc_.dropOneIn > 0 && random(sc_.dropOneIn) == 0) {
                    continue; // random loss on air
                }
                DashboardReading r = reading(n, sentAt);
                deliver(dash, r, nowMs, cb);
                if (n.id == 2 && inCycle >= sc_.dupAtMs && dupCycle_ != cycle) {
                    dupCycle_ = cycle;
                    dup_ = r; // same packet arrives again via a relay
                    dupDueMs_ = nowMs + 1500;
                    dupPending_ = true;
                }
            }
        }
        if (dupPending_ && (int32_t)(nowMs - dupDueMs_) >= 0) {
            dupPending_ = false;
            deliver(dash, dup_, nowMs, cb);
        }
    }

  private:
    static const uint16_t kNodes = 3;

    struct Node {
        uint16_t id;
        double lat, lng, heading;
        uint16_t seq;
        uint32_t bootOffsetMs;
        uint32_t nextMs;
        int batteryMv;
        float tempBase, humBase;
    };

    DemoScenario sc_;
    Node nodes_[kNodes];
    DashboardReading dup_;
    uint32_t dupDueMs_;
    bool dupPending_;
    uint32_t gapCycle_;
    uint32_t dupCycle_;

    static double rnd(double lo, double hi) {
        return lo + (hi - lo) * (random(10001) / 10000.0);
    }

    void walk(Node &n) const {
        n.heading = fmod(n.heading + rnd(-35, 35) + 360.0, 360.0);
        const double dist = rnd(sc_.stepMinM, sc_.stepMaxM);
        const double rad = n.heading * M_PI / 180.0;
        n.lat += dist * cos(rad) / 110540.0;
        n.lng += dist * sin(rad) / (111320.0 * cos(n.lat * M_PI / 180.0));
    }

    DashboardReading reading(Node &n, uint32_t t) const {
        const double baseLat = 37.2000, baseLng = -113.6400;
        const double dx =
            (n.lng - baseLng) * 111320.0 * cos(baseLat * M_PI / 180.0);
        const double dy = (n.lat - baseLat) * 110540.0;
        // Sensor drift follows "field time", so it looks the same at 6x
        const double minutes = t / 60000.0 * (30000.0 / sc_.sendMs);
        n.batteryMv -= random(0, 3);
        if (n.batteryMv < 3300) {
            n.batteryMv = 3300;
        }

        DashboardReading r;
        r.nodeId = n.id;
        r.seqNum = n.seq;
        r.elapsedMs = (int32_t)(t + n.bootOffsetMs);
        r.lat = n.lat;
        r.lng = n.lng;
        r.tempC = (float)(n.tempBase + 1.5 * sin(minutes / 7 + n.id) +
                          rnd(-0.3, 0.3));
        float h = (float)(n.humBase + 3 * cos(minutes / 9 + n.id) + rnd(-1, 1));
        r.humidity = h < 0 ? 0 : (h > 100 ? 100 : h);
        r.hopLimit = (n.id == 3 && random(10) < 4) ? 2 : 3;
        r.batteryMv = (uint16_t)(n.batteryMv + random(-4, 5));
        int rssi = (int)(-68 - sqrt(dx * dx + dy * dy) / 25 + rnd(-4, 4));
        r.rssi = (int16_t)(rssi < -125 ? -125 : rssi);
        return r;
    }

    static void deliver(WebDashboard &dash, const DashboardReading &r,
                        uint32_t nowMs, PacketCallback cb) {
        UpdateResult result = dash.update(r, nowMs);
        if (cb != nullptr) {
            cb(r, result);
        }
    }
};

#endif // WEB_DASHBOARD_DEMO_H
