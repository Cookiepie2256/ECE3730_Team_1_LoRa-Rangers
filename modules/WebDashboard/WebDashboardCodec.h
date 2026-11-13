#ifndef WEB_DASHBOARD_CODEC_H
#define WEB_DASHBOARD_CODEC_H

// Glue between the team's PacketCodec and the WebDashboard. Kept in its own
// header so WebDashboard itself does not depend on PacketCodec.
//
//   DecodedPacket pkt;
//   if (PacketCodec::decode(rxBuffer, len, pkt)) {
//       forwarder.forward(pkt, rssi);              // Braxton: PC CSV log
//       dashboard.update(toDashboardReading(pkt, rssi), millis()); // GUI
//   }

#include "WebDashboard.h"
#include <PacketCodec.h>

inline DashboardReading toDashboardReading(const DecodedPacket &pkt,
                                           int16_t rssiDbm) {
    DashboardReading r;
    r.nodeId = pkt.nodeId;
    r.seqNum = pkt.seqNum;
    r.elapsedMs = pkt.timestampMs;
    r.lat = pkt.latitude;
    r.lng = pkt.longitude;
    r.tempC = pkt.tempC;
    r.humidity = pkt.humPct;
    r.hopLimit = pkt.hops;
    r.batteryMv = pkt.batteryMv;
    r.rssi = rssiDbm;
    return r;
}

#endif // WEB_DASHBOARD_CODEC_H
