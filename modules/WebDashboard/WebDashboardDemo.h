#ifndef WEB_DASHBOARD_DEMO_H
#define WEB_DASHBOARD_DEMO_H

// Fake LoRa traffic for testing the dashboard with no sensor nodes.
// Same scenario as tools/preview_server.py:
//   - 3 rescuers walking near Snow Canyon, Utah, one packet each ~30 s +/- 4 s
//   - about 1 in 12 packets lost on air (the next one shows as GAP)
//   - Node 2 sends one duplicate every 10 minutes (seq % 20 == 4)
//   - Node 3 is silent from minute 4:00 to 7:00 of every 10 minutes, so it
//     turns LATE (60 s), then LOST (150 s), then recovers with a GAP

#include "WebDashboard.h"
#include <math.h>

class DashboardDemo {
  public:
    // Called for every packet handed to the dashboard
    typedef void (*PacketCallback)(const DashboardReading &r,
                                   UpdateResult result);

    DashboardDemo() : dupDueMs_(0), dupPending_(false) {
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
            n.nextMs = 2000UL + 3000UL * i;
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
                n.nextMs += kSendMs + random(-kJitterMs, kJitterMs + 1);
                walk(n);
                n.seq++;
                if (n.id == 3 && silent(sentAt)) {
                    continue; // out of range: sent but never heard
                }
                if (random(kDropOneIn) == 0) {
                    continue; // lost on air
                }
                DashboardReading r = reading(n, sentAt);
                deliver(dash, r, nowMs, cb);
                if (n.id == 2 && n.seq % 20 == 4) {
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
    static const long kSendMs = 30000;
    static const long kJitterMs = 4000;
    static const long kDropOneIn = 12;
    static const uint32_t kCycleMs = 600000;
    static const uint32_t kSilentFromMs = 240000;
    static const uint32_t kSilentToMs = 420000;

    struct Node {
        uint16_t id;
        double lat, lng, heading;
        uint16_t seq;
        uint32_t bootOffsetMs;
        uint32_t nextMs;
        int batteryMv;
        float tempBase, humBase;
    };

    Node nodes_[kNodes];
    DashboardReading dup_;
    uint32_t dupDueMs_;
    bool dupPending_;

    static bool silent(uint32_t t) {
        const uint32_t inCycle = t % kCycleMs;
        return inCycle >= kSilentFromMs && inCycle < kSilentToMs;
    }

    static double rnd(double lo, double hi) {
        return lo + (hi - lo) * (random(10001) / 10000.0);
    }

    static void walk(Node &n) {
        n.heading = fmod(n.heading + rnd(-35, 35) + 360.0, 360.0);
        const double dist = rnd(15, 40); // meters per 30 s
        const double rad = n.heading * M_PI / 180.0;
        n.lat += dist * cos(rad) / 110540.0;
        n.lng += dist * sin(rad) / (111320.0 * cos(n.lat * M_PI / 180.0));
    }

    static DashboardReading reading(Node &n, uint32_t t) {
        const double baseLat = 37.2000, baseLng = -113.6400;
        const double dx = (n.lng - baseLng) * 111320.0 * cos(baseLat * M_PI / 180.0);
        const double dy = (n.lat - baseLat) * 110540.0;
        const double minutes = t / 60000.0;
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
