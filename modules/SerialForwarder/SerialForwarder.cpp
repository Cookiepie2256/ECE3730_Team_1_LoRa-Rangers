#include "SerialForwarder.h"
#include <stdio.h>

const char *const SerialForwarder::PREFIX = "[CSVDATA] ";

SerialForwarder::SerialForwarder(Print &out) : out_(out) {}

bool SerialForwarder::forward(const DecodedPacket &pkt, int16_t rssiDbm) {
    char line[MAX_LINE_LEN];
    size_t len = formatLine(pkt, rssiDbm, line, sizeof(line));
    if (len == 0) {
        return false;
    }
    return out_.write((const uint8_t *)line, len) == len;
}

size_t SerialForwarder::formatLine(const DecodedPacket &pkt, int16_t rssiDbm,
                                   char *out, size_t outLen) {
    if (out == nullptr || outLen == 0) {
        return 0;
    }

    int n = snprintf(out, outLen, "%s%u,%u,%ld,%.6f,%.6f,%.1f,%.1f,%u,%u,%d\n",
                     PREFIX, (unsigned)pkt.nodeId, (unsigned)pkt.seqNum,
                     (long)pkt.timestampMs, pkt.latitude, pkt.longitude,
                     (double)pkt.tempC, (double)pkt.humPct, (unsigned)pkt.hops,
                     (unsigned)pkt.batteryMv, (int)rssiDbm);

    if (n < 0 || (size_t)n >= outLen) {
        out[0] = '\0';
        return 0; // Didn't fit
    }
    return (size_t)n;
}
