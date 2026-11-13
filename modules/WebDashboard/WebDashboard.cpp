#include "WebDashboard.h"
#include "WebDashboardPage.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const WebDashboard::LOG_HEADER =
    "tag,received_at_ms,node_id,seq,result,elapsed_ms,latitude,longitude,"
    "temp_c,humidity_pct,hops,battery_mv,rssi_dbm,received,missed,duplicates";

namespace {

// A node's clock (millis since boot) only goes forward. If a packet's elapsed
// time is more than this far behind the last one, the clock went backwards.
const int64_t kClockSlackMs = 1000;

// Room kept free while writing packets so the closing "],...}" always fits
const size_t kJsonTailReserve = 48;

const size_t kCsvFields = 10;

// /data response buffer. Static so it never touches the heap.
char s_json[DASH_JSON_BUF];

// Appends formatted text to a fixed buffer and remembers if anything didn't
// fit. Nothing is written past `cap`.
struct JsonOut {
    char *buf;
    size_t cap;
    size_t pos;
    bool ok;

    void add(const char *fmt, ...) {
        if (!ok) {
            return;
        }
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf + pos, cap - pos, fmt, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= cap - pos) {
            ok = false;
            buf[pos] = '\0';
            return;
        }
        pos += (size_t)n;
    }

    // JSON has no NaN: invalid sensor values become null
    void number(double v, int decimals) {
        if (isfinite(v)) {
            add("%.*f", decimals, v);
        } else {
            add("null");
        }
    }
};

void writeReadingFields(JsonOut &o, const DashboardReading &r) {
    o.add("\"lat\":");
    o.number(r.lat, 6);
    o.add(",\"lng\":");
    o.number(r.lng, 6);
    o.add(",\"temp\":");
    o.number(r.tempC, 1);
    o.add(",\"humidity\":");
    o.number(r.humidity, 1);
    o.add(",\"batteryMv\":%u,\"rssi\":%d,\"hopLimit\":%u",
          (unsigned)r.batteryMv, (int)r.rssi, (unsigned)r.hopLimit);
}

char *skipSpaces(char *s) {
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

// True if only spaces are left after a parsed number
bool onlySpacesLeft(const char *end) {
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    return *end == '\0';
}

bool parseInt(const char *s, long long minV, long long maxV, long long &out) {
    if (*s == '\0') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (end == s || errno == ERANGE || !onlySpacesLeft(end) || v < minV ||
        v > maxV) {
        return false;
    }
    out = v;
    return true;
}

// allowNan: a sensor that failed may report "nan"
bool parseReal(const char *s, double minV, double maxV, bool allowNan,
               double &out) {
    if (*s == '\0') {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    double v = strtod(s, &end);
    if (end == s || errno == ERANGE || !onlySpacesLeft(end)) {
        return false;
    }
    if (isnan(v)) {
        if (!allowNan) {
            return false;
        }
    } else if (!isfinite(v) || v < minV || v > maxV) {
        return false;
    }
    out = v;
    return true;
}

} // namespace

WebDashboard::WebDashboard(uint16_t port)
    : nodes_(), history_(), nextUid_(1), lateMs_(DASH_LATE_MS),
      lostMs_(DASH_LOST_MS), modeLabel_(nullptr), bootId_(0), started_(false),
      server_(port) {}

bool WebDashboard::begin(const char *ssid, const char *pass) {
    if (bootId_ == 0) {
        bootId_ = (uint32_t)random(1, 0x7FFFFFFF);
    }
    WiFi.mode(WIFI_AP);
    IPAddress ip(192, 168, 4, 1);
    IPAddress mask(255, 255, 255, 0);
    WiFi.softAPConfig(ip, ip, mask);
    if (!WiFi.softAP(ssid, pass)) {
        return false;
    }
    if (!started_) {
        server_.on("/", HTTP_GET, [this]() { handleRoot(); });
        server_.on("/data", HTTP_GET, [this]() { handleData(); });
        server_.onNotFound([this]() { handleNotFound(); });
        server_.begin();
        started_ = true;
    }
    return true;
}

void WebDashboard::handleClient() {
    if (started_) {
        server_.handleClient();
    }
}

uint8_t WebDashboard::slotOf(uint16_t nodeId) const {
    if (nodeId == 0) {
        return 0;
    }
    for (uint8_t i = 0; i < DASH_MAX_NODES; i++) {
        if (nodes_[i].nodeId == nodeId) {
            return i + 1;
        }
    }
    return 0;
}

uint8_t WebDashboard::claimSlot(uint16_t nodeId) {
    // Small IDs keep their own slot (node 2 -> slot 2) so hand-numbered nodes
    // and the demo look the same as before
    if (nodeId <= DASH_MAX_NODES && nodes_[nodeId - 1].nodeId == 0) {
        nodes_[nodeId - 1].nodeId = nodeId;
        return nodeId;
    }
    for (uint8_t i = 0; i < DASH_MAX_NODES; i++) {
        if (nodes_[i].nodeId == 0) {
            nodes_[i].nodeId = nodeId;
            return i + 1;
        }
    }
    return 0;
}

UpdateResult WebDashboard::update(const DashboardReading &r, uint32_t nowMs) {
    if (r.nodeId == 0) {
        return UpdateResult::BAD_NODE;
    }
    uint8_t slot = slotOf(r.nodeId);
    if (slot == 0) {
        slot = claimSlot(r.nodeId);
        if (slot == 0) {
            return UpdateResult::BAD_NODE; // Every slot belongs to another node
        }
        accept(slot, r, UpdateResult::NEW, nowMs);
        return UpdateResult::NEW;
    }
    NodeState &st = nodes_[slot - 1];

    // Distances in both directions, so 65535 -> 0 counts as one step forward
    const uint16_t fwd = (uint16_t)(r.seqNum - st.last.seqNum);
    const uint16_t back = (uint16_t)(st.last.seqNum - r.seqNum);
    const bool clockWentBack =
        (int64_t)r.elapsedMs + kClockSlackMs < (int64_t)st.last.elapsedMs;

    if (fwd == 0 && !clockWentBack) {
        st.duplicates++;
        return UpdateResult::DUPLICATE;
    }
    if (fwd != 0 && back <= DASH_OLD_WINDOW) {
        st.oldPackets++; // Delayed packet older than what we already show
        return UpdateResult::OLD;
    }
    if (fwd == 0 || clockWentBack || fwd - 1 > DASH_MAX_GAP) {
        // The node restarted (clock went back, sequence jumped). Start
        // tracking again from this packet instead of counting thousands of
        // "missed" packets.
        st.reboots++;
        accept(slot, r, UpdateResult::NEW, nowMs);
        return UpdateResult::NEW;
    }
    if (fwd > 1) {
        st.missed += fwd - 1;
        accept(slot, r, UpdateResult::GAP, nowMs);
        return UpdateResult::GAP;
    }
    accept(slot, r, UpdateResult::NEW, nowMs);
    return UpdateResult::NEW;
}

void WebDashboard::accept(uint8_t slot, const DashboardReading &r,
                          UpdateResult result, uint32_t nowMs) {
    NodeState &st = nodes_[slot - 1];
    st.hasData = true;
    st.last = r;
    st.lastResult = result;
    st.receivedAtMs = nowMs;
    st.received++;

    DashboardPacket &entry = history_[(nextUid_ - 1) % DASH_HISTORY];
    entry.uid = nextUid_;
    entry.receivedAtMs = nowMs;
    entry.result = result;
    entry.slot = slot;
    entry.reading = r;
    nextUid_++;
}

bool WebDashboard::updateFromCsv(const char *line, uint32_t nowMs,
                                 UpdateResult *out) {
    if (line == nullptr) {
        return false;
    }
    size_t n = 0;
    while (n <= DASH_CSV_MAX_LEN && line[n] != '\0') {
        n++;
    }
    if (n > DASH_CSV_MAX_LEN) {
        return false;
    }

    char tmp[DASH_CSV_MAX_LEN + 1];
    memcpy(tmp, line, n);
    tmp[n] = '\0';

    // Trim the end (newline from Serial) and the optional "[CSVDATA]" prefix
    while (n > 0 && isspace((unsigned char)tmp[n - 1])) {
        tmp[--n] = '\0';
    }
    char *p = skipSpaces(tmp);
    if (strncmp(p, "[CSVDATA]", 9) == 0) {
        p = skipSpaces(p + 9);
    }

    char *fields[kCsvFields];
    size_t count = 0;
    char *s = p;
    while (true) {
        if (count == kCsvFields) {
            return false; // Too many fields
        }
        fields[count++] = skipSpaces(s);
        char *comma = strchr(s, ',');
        if (comma == nullptr) {
            break;
        }
        *comma = '\0';
        s = comma + 1;
    }
    if (count != kCsvFields) {
        return false;
    }

    long long node, seq, elapsed, hops, battery, rssi;
    double lat, lng, temp, hum;
    if (!parseInt(fields[0], 0, 65535, node) ||
        !parseInt(fields[1], 0, 65535, seq) ||
        !parseInt(fields[2], INT32_MIN, INT32_MAX, elapsed) ||
        !parseReal(fields[3], -90.0, 90.0, false, lat) ||
        !parseReal(fields[4], -180.0, 180.0, false, lng) ||
        !parseReal(fields[5], -100.0, 150.0, true, temp) ||
        !parseReal(fields[6], 0.0, 100.0, true, hum) ||
        !parseInt(fields[7], 0, 255, hops) ||
        !parseInt(fields[8], 0, 65535, battery) ||
        !parseInt(fields[9], -200, 50, rssi)) {
        return false;
    }

    DashboardReading r;
    r.nodeId = (uint16_t)node;
    r.seqNum = (uint16_t)seq;
    r.elapsedMs = (int32_t)elapsed;
    r.lat = lat;
    r.lng = lng;
    r.tempC = (float)temp;
    r.humidity = (float)hum;
    r.hopLimit = (uint8_t)hops;
    r.batteryMv = (uint16_t)battery;
    r.rssi = (int16_t)rssi;

    UpdateResult result = update(r, nowMs);
    if (out != nullptr) {
        *out = result;
    }
    return result != UpdateResult::BAD_NODE;
}

NodeStatus WebDashboard::getStatus(uint16_t nodeId, uint32_t nowMs) const {
    const NodeState *st = getNode(nodeId);
    return st == nullptr ? NodeStatus::NO_DATA : statusOf(*st, nowMs);
}

NodeStatus WebDashboard::statusOf(const NodeState &st, uint32_t nowMs) const {
    if (!st.hasData) {
        return NodeStatus::NO_DATA;
    }
    const uint32_t age = nowMs - st.receivedAtMs; // Safe across millis() wrap
    if (age >= lostMs_) {
        return NodeStatus::LOST;
    }
    if (age >= lateMs_) {
        return NodeStatus::LATE;
    }
    return NodeStatus::OK;
}

void WebDashboard::setTimeouts(uint32_t lateMs, uint32_t lostMs) {
    lateMs_ = lateMs;
    lostMs_ = lostMs < lateMs ? lateMs : lostMs;
}

const NodeState *WebDashboard::getNode(uint16_t nodeId) const {
    const uint8_t slot = slotOf(nodeId);
    return slot == 0 ? nullptr : &nodes_[slot - 1];
}

size_t WebDashboard::buildJson(char *buf, size_t len, uint32_t nowMs,
                               uint32_t sinceId) const {
    if (buf == nullptr || len <= kJsonTailReserve) {
        if (buf != nullptr && len > 0) {
            buf[0] = '\0';
        }
        return 0;
    }

    JsonOut o{buf, len - kJsonTailReserve, 0, true};
    o.add("{\"nowMs\":%lu,\"uptimeMs\":%lu,\"bootId\":%lu,\"lateMs\":%lu,"
          "\"lostMs\":%lu,",
          (unsigned long)nowMs, (unsigned long)nowMs, (unsigned long)bootId_,
          (unsigned long)lateMs_, (unsigned long)lostMs_);
    if (modeLabel_ != nullptr) {
        // Quotes, backslashes and control characters are skipped so the
        // JSON stays valid whatever the caller passes
        o.add("\"mode\":\"");
        for (const char *c = modeLabel_; *c != '\0'; c++) {
            if (*c != '"' && *c != '\\' && (unsigned char)*c >= 0x20) {
                o.add("%c", *c);
            }
        }
        o.add("\",");
    }
    o.add("\"nodes\":[");

    for (uint16_t id = 1; id <= DASH_MAX_NODES; id++) {
        const NodeState &st = nodes_[id - 1];
        o.add("%s{\"id\":%u,\"status\":\"%s\",", id == 1 ? "" : ",",
              (unsigned)id, statusName(statusOf(st, nowMs)));
        if (st.nodeId != 0) {
            o.add("\"nodeId\":%u,", (unsigned)st.nodeId);
        }
        if (st.hasData) {
            o.add("\"ageMs\":%lu,\"seq\":%u,\"elapsedMs\":%ld,",
                  (unsigned long)(nowMs - st.receivedAtMs),
                  (unsigned)st.last.seqNum, (long)st.last.elapsedMs);
            writeReadingFields(o, st.last);
            o.add(",");
        }
        o.add("\"received\":%lu,\"missed\":%lu,\"duplicates\":%lu,"
              "\"old\":%lu,\"reboots\":%lu}",
              (unsigned long)st.received, (unsigned long)st.missed,
              (unsigned long)st.duplicates, (unsigned long)st.oldPackets,
              (unsigned long)st.reboots);
    }
    o.add("],\"packets\":[");
    if (!o.ok) {
        buf[0] = '\0';
        return 0; // Not even the node list fits
    }

    // Packets still in the ring buffer, oldest first
    const uint32_t newest = lastUid();
    const uint32_t oldest = newest > DASH_HISTORY ? newest - DASH_HISTORY + 1 : 1;
    uint32_t lastIncluded = sinceId;
    bool more = false;
    bool first = true;
    const uint32_t firstUid =
        sinceId >= newest ? newest + 1 : (sinceId + 1 > oldest ? sinceId + 1 : oldest);
    for (uint32_t uid = firstUid; uid <= newest && uid != 0; uid++) {
        const DashboardPacket &p = history_[(uid - 1) % DASH_HISTORY];
        const size_t mark = o.pos;
        o.add("%s{\"uid\":%lu,\"node\":%u,\"nodeId\":%u,\"seq\":%u,"
              "\"result\":\"%s\",\"receivedAtMs\":%lu,\"elapsedMs\":%ld,",
              first ? "" : ",", (unsigned long)p.uid, (unsigned)p.slot,
              (unsigned)p.reading.nodeId, (unsigned)p.reading.seqNum,
              resultName(p.result), (unsigned long)p.receivedAtMs,
              (long)p.reading.elapsedMs);
        writeReadingFields(o, p.reading);
        o.add("}");
        if (!o.ok) {
            // Out of room: drop this packet, the page asks again for the rest
            o.pos = mark;
            o.ok = true;
            buf[mark] = '\0';
            more = true;
            break;
        }
        first = false;
        lastIncluded = uid;
    }

    if (more && first) {
        buf[0] = '\0';
        return 0; // Not even one packet fits: paging could never progress
    }

    o.cap = len; // Use the reserved room for the closing part
    o.add("],\"lastId\":%lu,\"more\":%s}",
          (unsigned long)(more ? lastIncluded : newest),
          more ? "true" : "false");
    if (!o.ok) {
        buf[0] = '\0';
        return 0;
    }
    return o.pos;
}

size_t WebDashboard::logLine(uint16_t nodeId, char *buf, size_t len) const {
    const NodeState *st = getNode(nodeId);
    if (buf == nullptr || len == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (st == nullptr || !st->hasData) {
        return 0;
    }
    const DashboardReading &r = st->last;
    int n = snprintf(buf, len,
                     "DASH,%lu,%u,%u,%s,%ld,%.6f,%.6f,%.1f,%.1f,%u,%u,%d,%lu,"
                     "%lu,%lu",
                     (unsigned long)st->receivedAtMs, (unsigned)r.nodeId,
                     (unsigned)r.seqNum, resultName(st->lastResult),
                     (long)r.elapsedMs, r.lat, r.lng, (double)r.tempC,
                     (double)r.humidity, (unsigned)r.hopLimit,
                     (unsigned)r.batteryMv, (int)r.rssi,
                     (unsigned long)st->received, (unsigned long)st->missed,
                     (unsigned long)st->duplicates);
    if (n < 0 || (size_t)n >= len) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)n;
}

const char *WebDashboard::resultName(UpdateResult r) {
    switch (r) {
    case UpdateResult::NEW:
        return "NEW";
    case UpdateResult::GAP:
        return "GAP";
    case UpdateResult::DUPLICATE:
        return "DUPLICATE";
    case UpdateResult::OLD:
        return "OLD";
    case UpdateResult::BAD_NODE:
        return "BAD_NODE";
    }
    return "?";
}

const char *WebDashboard::statusName(NodeStatus s) {
    switch (s) {
    case NodeStatus::NO_DATA:
        return "NO_DATA";
    case NodeStatus::OK:
        return "OK";
    case NodeStatus::LATE:
        return "LATE";
    case NodeStatus::LOST:
        return "LOST";
    }
    return "?";
}

void WebDashboard::handleRoot() {
    server_.send_P(200, "text/html", DASH_PAGE, DASH_PAGE_LEN);
}

void WebDashboard::handleData() {
    uint32_t since = 0;
    if (server_.hasArg("since")) {
        since = (uint32_t)strtoul(server_.arg("since").c_str(), nullptr, 10);
    }
    const size_t n = buildJson(s_json, sizeof(s_json), millis(), since);
    if (n == 0) {
        server_.send(500, "text/plain", "JSON buffer too small");
        return;
    }
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "application/json", s_json, n);
}

void WebDashboard::handleNotFound() {
    server_.send(404, "text/plain", "Not found");
}
