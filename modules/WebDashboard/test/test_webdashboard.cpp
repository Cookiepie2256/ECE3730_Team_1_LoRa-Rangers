// Host tests for WebDashboard. Build and run with test/run_tests.sh or
// test/run_tests.bat; they also validate every JSON file this writes with
// `python -m json.tool`.
//
//   test_webdashboard <dir for JSON files>

#include "WebDashboard.h"
#include "WebDashboardPage.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;
static std::string g_jsonDir = ".";
static int g_jsonFiles = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) {                                                            \
            g_pass++;                                                          \
        } else {                                                               \
            g_fail++;                                                          \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                      \
    } while (0)

static DashboardReading reading(uint16_t node, uint16_t seq, int32_t elapsed) {
    DashboardReading r;
    r.nodeId = node;
    r.seqNum = seq;
    r.elapsedMs = elapsed;
    r.lat = 37.204512;
    r.lng = -113.640231;
    r.tempC = 25.4f;
    r.humidity = 31.0f;
    r.hopLimit = 3;
    r.batteryMv = 3950;
    r.rssi = -92;
    return r;
}

static int count(const std::string &haystack, const std::string &needle) {
    int n = 0;
    for (size_t pos = haystack.find(needle); pos != std::string::npos;
         pos = haystack.find(needle, pos + needle.size())) {
        n++;
    }
    return n;
}

// Saved for run_tests to check with python -m json.tool
static void saveJson(const std::string &name, const char *json) {
    std::string path = g_jsonDir + "/" + name + ".json";
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::printf("  FAIL cannot write %s\n", path.c_str());
        g_fail++;
        return;
    }
    std::fputs(json, f);
    std::fclose(f);
    g_jsonFiles++;
}

static std::string json(const WebDashboard &d, uint32_t now, uint32_t since) {
    static char buf[DASH_JSON_BUF];
    size_t n = d.buildJson(buf, sizeof(buf), now, since);
    return std::string(buf, n);
}

// ---------------------------------------------------------------------------

static void testValidCsv() {
    WebDashboard d;
    UpdateResult res = UpdateResult::OLD;
    CHECK(d.updateFromCsv("2,17,512340,37.204512,-113.640231,25.4,31.0,3,3950,-92",
                          1000, &res));
    CHECK(res == UpdateResult::NEW);
    const NodeState *s = d.getNode(2);
    CHECK(s != nullptr && s->hasData);
    CHECK(s->last.seqNum == 17);
    CHECK(s->last.elapsedMs == 512340);
    CHECK(std::fabs(s->last.lat - 37.204512) < 1e-9);
    CHECK(std::fabs(s->last.lng + 113.640231) < 1e-9);
    CHECK(std::fabs(s->last.tempC - 25.4f) < 1e-4);
    CHECK(std::fabs(s->last.humidity - 31.0f) < 1e-4);
    CHECK(s->last.hopLimit == 3);
    CHECK(s->last.batteryMv == 3950);
    CHECK(s->last.rssi == -92);
    CHECK(s->receivedAtMs == 1000);

    // Exact SerialForwarder output: prefix, CRLF, negative temperature
    CHECK(d.updateFromCsv("[CSVDATA] 1,5,1000,37.1,-113.5,-4.5,80.0,2,3700,-110\r\n",
                          2000, &res));
    CHECK(res == UpdateResult::NEW);
    CHECK(std::fabs(d.getNode(1)->last.tempC + 4.5f) < 1e-4);

    // Spaces around fields, and "nan" from a failed sensor
    CHECK(d.updateFromCsv(" 3 , 1 , 0 , 0 , 0 , nan , nan , 0 , 0 , -120 ", 3000, &res));
    CHECK(std::isnan(d.getNode(3)->last.tempC));
}

static void testMalformedCsv() {
    WebDashboard d;
    const char *bad[] = {
        "",
        "   ",
        "[CSVDATA] ",
        "1,2,3",
        "1,2,3,4,5,6,7,8,9",                               // 9 fields
        "1,2,3,37.2,-113.6,25,31,3,3950,-92,7",             // 11 fields
        "1,2,3,37.2,-113.6,25,31,3,3950,-92,",              // trailing comma
        "1,,3,37.2,-113.6,25,31,3,3950,-92",                // empty field
        "x,2,3,37.2,-113.6,25,31,3,3950,-92",               // not a number
        "1,2x,3,37.2,-113.6,25,31,3,3950,-92",              // junk after number
        "1,70000,3,37.2,-113.6,25,31,3,3950,-92",           // seq > 65535
        "1,2,3,91.0,-113.6,25,31,3,3950,-92",               // latitude
        "1,2,3,37.2,-181.0,25,31,3,3950,-92",               // longitude
        "1,2,3,nan,-113.6,25,31,3,3950,-92",                // lat must be real
        "1,2,3,37.2,-113.6,25,101,3,3950,-92",              // humidity > 100
        "1,2,3,37.2,-113.6,25,31,256,3950,-92",             // hops > 255
        "1,2,3,37.2,-113.6,25,31,3,70000,-92",              // battery
        "1,2,3,37.2,-113.6,25,31,3,3950,-300",              // rssi
        "1,2,99999999999,37.2,-113.6,25,31,3,3950,-92",     // elapsed > int32
        "1;2;3;37.2;-113.6;25;31;3;3950;-92",               // wrong separator
    };
    for (const char *line : bad) {
        UpdateResult res = UpdateResult::OLD; // sentinel: must stay untouched
        bool ok = d.updateFromCsv(line, 1000, &res);
        if (ok || res != UpdateResult::OLD) {
            std::printf("  (accepted bad line: \"%s\")\n", line);
        }
        CHECK(!ok);
        CHECK(res == UpdateResult::OLD);
    }
    CHECK(!d.updateFromCsv(nullptr, 1000));

    std::string tooLong(DASH_CSV_MAX_LEN + 1, '1');
    CHECK(!d.updateFromCsv(tooLong.c_str(), 1000));

    for (uint16_t id = 1; id <= DASH_MAX_NODES; id++) {
        CHECK(d.getNode(id) == nullptr); // nothing was stored
    }
    CHECK(d.lastUid() == 0);
}

static void testBadNodeIds() {
    WebDashboard d;
    UpdateResult res = UpdateResult::NEW;
    CHECK(!d.updateFromCsv("0,1,1000,37.2,-113.6,25,31,3,3950,-92", 1000, &res));
    CHECK(res == UpdateResult::BAD_NODE);
    CHECK(d.update(reading(0, 1, 0), 0) == UpdateResult::BAD_NODE);
    CHECK(d.getNode(0) == nullptr);
    CHECK(d.lastUid() == 0);

    // A 4th node is rejected once 3 others own every slot
    CHECK(d.update(reading(0xBB01, 1, 0), 0) == UpdateResult::NEW);
    CHECK(d.update(reading(0xBB02, 1, 0), 0) == UpdateResult::NEW);
    CHECK(d.update(reading(0xBB03, 1, 0), 0) == UpdateResult::NEW);
    res = UpdateResult::NEW;
    CHECK(!d.updateFromCsv("4,1,1000,37.2,-113.6,25,31,3,3950,-92", 1000, &res));
    CHECK(res == UpdateResult::BAD_NODE);
    CHECK(d.update(reading(0xBB04, 1, 0), 0) == UpdateResult::BAD_NODE);
    CHECK(d.getNode(4) == nullptr);
    CHECK(d.getNode(0xBB04) == nullptr);
    CHECK(d.getStatus(0xBB04, 0) == NodeStatus::NO_DATA);
    CHECK(d.lastUid() == 3);
    // Known nodes keep working
    CHECK(d.update(reading(0xBB02, 2, 30000), 30000) == UpdateResult::NEW);
}

// MAC-based node IDs (NodeId::get()) get page slots on first contact
static void testSlotAssignment() {
    WebDashboard d;
    CHECK(d.update(reading(2, 1, 0), 0) == UpdateResult::NEW);
    CHECK(d.slotOf(2) == 2); // small IDs keep their own slot
    CHECK(d.update(reading(0xBB01, 1, 0), 0) == UpdateResult::NEW);
    CHECK(d.slotOf(0xBB01) == 1); // first free slot
    CHECK(d.update(reading(1, 1, 0), 0) == UpdateResult::NEW);
    CHECK(d.slotOf(1) == 3); // slot 1 is taken, so the next free one
    CHECK(d.slotOf(0xBB02) == 0);

    const NodeState *s = d.getNode(0xBB01);
    CHECK(s != nullptr && s->hasData && s->nodeId == 0xBB01);
    CHECK(d.update(reading(0xBB01, 3, 60000), 60000) == UpdateResult::GAP);
    CHECK(s->missed == 1);
    CHECK(d.getStatus(0xBB01, 60000) == NodeStatus::OK);

    CHECK(d.updateFromCsv("48003,7,1000,37.2,-113.6,25,31,3,3950,-92", 1000) ==
          false); // all 3 slots in use

    char buf[200];
    CHECK(d.logLine(0xBB01, buf, sizeof(buf)) > 0);
    CHECK(std::strstr(buf, ",47873,3,GAP,") != nullptr);

    std::string j = json(d, 60000, 0);
    CHECK(j.find("{\"id\":1,\"status\":\"OK\",\"nodeId\":47873,") != std::string::npos);
    CHECK(j.find("{\"id\":3,\"status\":\"LATE\",\"nodeId\":1,") != std::string::npos);
    CHECK(j.find("\"node\":1,\"nodeId\":47873,\"seq\":3,") != std::string::npos);
    saveJson("json_mac_ids", j.c_str());
}

static void testGap() {
    WebDashboard d;
    CHECK(d.update(reading(1, 1, 30000), 0) == UpdateResult::NEW);
    CHECK(d.update(reading(1, 2, 60000), 30000) == UpdateResult::NEW);
    CHECK(d.update(reading(1, 5, 150000), 120000) == UpdateResult::GAP);
    const NodeState *s = d.getNode(1);
    CHECK(s->missed == 2);
    CHECK(s->received == 3);
    CHECK(s->last.seqNum == 5);
    CHECK(s->lastResult == UpdateResult::GAP);
    CHECK(count(json(d, 120000, 0), "\"result\":\"GAP\"") == 1);
}

static void testDuplicate() {
    WebDashboard d;
    d.update(reading(2, 7, 210000), 1000);
    CHECK(d.update(reading(2, 7, 210000), 2500) == UpdateResult::DUPLICATE);
    const NodeState *s = d.getNode(2);
    CHECK(s->duplicates == 1);
    CHECK(s->received == 1);
    CHECK(s->receivedAtMs == 1000); // duplicate does not refresh "last seen"
    CHECK(d.lastUid() == 1);        // and is not added to history
}

static void testOldPacket() {
    WebDashboard d;
    d.update(reading(3, 5, 150000), 0);
    CHECK(d.update(reading(3, 3, 90000), 1000) == UpdateResult::OLD);
    const NodeState *s = d.getNode(3);
    CHECK(s->oldPackets == 1);
    CHECK(s->last.seqNum == 5);
    CHECK(s->missed == 0);
    CHECK(d.lastUid() == 1);
}

static void testSeqWrap() {
    WebDashboard d;
    d.update(reading(1, 65535, 1000000), 0);
    CHECK(d.update(reading(1, 0, 1030000), 30000) == UpdateResult::NEW);
    CHECK(d.getNode(1)->missed == 0);

    WebDashboard d2;
    d2.update(reading(1, 65534, 1000000), 0);
    CHECK(d2.update(reading(1, 1, 1090000), 90000) == UpdateResult::GAP);
    CHECK(d2.getNode(1)->missed == 2); // 65535 and 0
}

static void testReboot() {
    WebDashboard d;
    d.update(reading(2, 500, 15000000), 0);
    // Node restarted: clock back near zero, seq starts over
    CHECK(d.update(reading(2, 1, 2000), 30000) == UpdateResult::NEW);
    const NodeState *s = d.getNode(2);
    CHECK(s->reboots == 1);
    CHECK(s->missed == 0);
    CHECK(s->last.seqNum == 1);
    CHECK(d.update(reading(2, 2, 32000), 60000) == UpdateResult::NEW);
    CHECK(s->missed == 0);

    // Same seq but clock went back: a restart, not a duplicate
    WebDashboard d2;
    d2.update(reading(1, 1, 900000), 0);
    CHECK(d2.update(reading(1, 1, 3000), 1000) == UpdateResult::NEW);
    CHECK(d2.getNode(1)->reboots == 1);

    // Huge forward jump with the clock moving forward: resync, no
    // thousands of "missed" packets
    WebDashboard d3;
    d3.update(reading(1, 10, 300000), 0);
    CHECK(d3.update(reading(1, 5000, 330000), 30000) == UpdateResult::NEW);
    CHECK(d3.getNode(1)->missed == 0);
    CHECK(d3.getNode(1)->reboots == 1);
}

static void testStatusTiming() {
    WebDashboard d;
    const uint32_t t0 = 5000;
    CHECK(d.getStatus(1, t0) == NodeStatus::NO_DATA);
    d.update(reading(1, 1, 1000), t0);
    CHECK(d.getStatus(1, t0) == NodeStatus::OK);
    CHECK(d.getStatus(1, t0 + 59999) == NodeStatus::OK);
    CHECK(d.getStatus(1, t0 + 60000) == NodeStatus::LATE);
    CHECK(d.getStatus(1, t0 + 149999) == NodeStatus::LATE);
    CHECK(d.getStatus(1, t0 + 150000) == NodeStatus::LOST);
    CHECK(count(json(d, t0 + 150000, 0), "\"status\":\"LOST\"") == 1);
    d.update(reading(1, 2, 200000), t0 + 200000);
    CHECK(d.getStatus(1, t0 + 200000) == NodeStatus::OK); // recovered

    d.setTimeouts(1000, 2000);
    CHECK(d.getStatus(1, t0 + 200999) == NodeStatus::OK);
    CHECK(d.getStatus(1, t0 + 201000) == NodeStatus::LATE);
    CHECK(d.getStatus(1, t0 + 202000) == NodeStatus::LOST);

    // millis() wraps after ~49.7 days; ages must still be right
    WebDashboard w;
    w.update(reading(1, 1, 0), 0xFFFFF000UL);
    CHECK(w.getStatus(1, 0x00000100UL) == NodeStatus::OK); // 4352 ms old
    CHECK(w.getStatus(1, 0x0000F000UL) == NodeStatus::LATE);
}

static void testJsonValid() {
    WebDashboard d;
    d.update(reading(1, 1, 30000), 1000);
    d.update(reading(2, 1, 30000), 2000);
    d.update(reading(2, 4, 120000), 92000);
    DashboardReading nanTemp = reading(3, 1, 0);
    nanTemp.tempC = NAN;
    nanTemp.humidity = NAN;
    d.update(nanTemp, 93000);

    std::string j = json(d, 100000, 0);
    CHECK(!j.empty());
    CHECK(j.front() == '{' && j.back() == '}');
    CHECK(j.find("\"nowMs\":100000") != std::string::npos);
    CHECK(j.find("\"lastId\":4") != std::string::npos);
    CHECK(j.find("\"more\":false") != std::string::npos);
    CHECK(count(j, "\"uid\":") == 4);
    CHECK(j.find("\"temp\":null") != std::string::npos);
    CHECK(j.find("nan") == std::string::npos);
    CHECK(j.find("\"missed\":2") != std::string::npos);
    saveJson("json_basic", j.c_str());

    WebDashboard empty;
    std::string e = json(empty, 0, 0);
    CHECK(count(e, "\"status\":\"NO_DATA\"") == 3);
    CHECK(e.find("\"packets\":[]") != std::string::npos);
    saveJson("json_empty", e.c_str());
}

static void testJsonSince() {
    WebDashboard d;
    for (uint16_t i = 1; i <= 10; i++) {
        d.update(reading(1 + i % 3, i, i * 30000), i * 1000);
    }
    CHECK(count(json(d, 20000, 0), "\"uid\":") == 10);
    std::string j7 = json(d, 20000, 7);
    CHECK(count(j7, "\"uid\":") == 3);
    CHECK(j7.find("\"uid\":7,") == std::string::npos);
    CHECK(j7.find("\"uid\":8,") != std::string::npos);
    saveJson("json_since7", j7.c_str());
    std::string jAll = json(d, 20000, 10);
    CHECK(count(jAll, "\"uid\":") == 0);
    CHECK(jAll.find("\"lastId\":10") != std::string::npos);
    // since beyond the newest (base station restarted): lastId < since
    CHECK(json(d, 20000, 999).find("\"lastId\":10") != std::string::npos);
}

static void testRingBuffer() {
    WebDashboard d;
    for (uint16_t i = 1; i <= 100; i++) {
        d.update(reading(1, i, i * 30000), i * 30000);
    }
    CHECK(d.lastUid() == 100);
    std::string j = json(d, 3000000, 0);
    CHECK(count(j, "\"uid\":") == DASH_HISTORY);
    CHECK(j.find("\"uid\":36,") == std::string::npos);
    CHECK(j.find("\"uid\":37,") != std::string::npos);
    CHECK(j.find("\"uid\":100,") != std::string::npos);
    saveJson("json_ring", j.c_str());
}

static void testJsonTinyBuffers() {
    WebDashboard d;
    for (uint16_t i = 1; i <= 30; i++) {
        d.update(reading(1 + i % 3, i, i * 30000), i * 1000);
    }
    CHECK(d.buildJson(nullptr, 100, 0, 0) == 0);

    const size_t sizes[] = {0, 1, 2, 10, 47, 48, 49, 100, 400, 700, 1000, 1500, 3000};
    for (size_t len : sizes) {
        std::vector<char> buf(len + 32, (char)0xAB);
        size_t n = d.buildJson(buf.data(), len, 40000, 0);
        bool canaryOk = true;
        for (size_t i = len; i < buf.size(); i++) {
            canaryOk = canaryOk && buf[i] == (char)0xAB;
        }
        CHECK(canaryOk); // nothing written past len
        if (n > 0) {
            CHECK(n < len);
            CHECK(buf[n] == '\0');
            std::string j(buf.data(), n);
            CHECK(j.back() == '}');
            saveJson("json_len" + std::to_string(len), buf.data());
        } else if (len > 0) {
            CHECK(buf[0] == '\0');
        }
    }

    // Paging: a small buffer still delivers every packet over several calls
    std::vector<char> buf(1500);
    uint32_t since = 0;
    int got = 0;
    for (int call = 0; call < 50; call++) {
        size_t n = d.buildJson(buf.data(), buf.size(), 40000, since);
        CHECK(n > 0);
        std::string j(buf.data(), n);
        got += count(j, "\"uid\":");
        size_t pos = j.find("\"lastId\":");
        since = (uint32_t)std::strtoul(j.c_str() + pos + 9, nullptr, 10);
        if (j.find("\"more\":false") != std::string::npos) {
            break;
        }
    }
    CHECK(got == 30);
    CHECK(since == 30);
}

static void testModeBootAndEdges() {
    WebDashboard d;
    CHECK(json(d, 0, 0).find("\"mode\"") == std::string::npos); // off by default
    d.setModeLabel("Presentation \"demo\"\\\n");
    std::string j = json(d, 0, 0);
    CHECK(j.find("\"mode\":\"Presentation demo\"") != std::string::npos);
    saveJson("json_mode", j.c_str());

    // bootId is random per power-up and set by begin()
    CHECK(j.find("\"bootId\":0,") != std::string::npos);
    CHECK(d.begin());
    CHECK(json(d, 0, 0).find("\"bootId\":0,") == std::string::npos);

    // since past the newest (even the largest value) returns no packets
    d.update(reading(1, 1, 1000), 0);
    std::string far = json(d, 0, 4294967295UL);
    CHECK(count(far, "\"uid\":") == 0);

    // A buffer too small for even one packet is an error, not endless paging
    std::vector<char> buf(1000);
    WebDashboard full;
    for (uint16_t id = 1; id <= 3; id++) {
        full.update(reading(id, 1, 1000), 0);
    }
    CHECK(full.buildJson(buf.data(), 900, 0, 0) == 0);
}

static void testLogLine() {
    WebDashboard d;
    char buf[200];
    CHECK(d.logLine(1, buf, sizeof(buf)) == 0); // no data yet
    CHECK(buf[0] == '\0');
    d.update(reading(1, 1, 30000), 1000);
    d.update(reading(1, 3, 90000), 61000);
    size_t n = d.logLine(1, buf, sizeof(buf));
    CHECK(n > 0 && n == std::strlen(buf));
    CHECK(std::strncmp(buf, "DASH,61000,1,3,GAP,90000,37.204512,-113.640231,25.4,31.0,3,3950,-92,2,1,0",
                       n) == 0);
    // Same number of columns as the header
    CHECK(count(buf, ",") == count(WebDashboard::LOG_HEADER, ","));
    CHECK(d.logLine(1, buf, 10) == 0);
    CHECK(d.logLine(9, buf, sizeof(buf)) == 0);
}

static void testRoutes() {
    WebDashboard d;
    CHECK(d.begin());
    WebServer *server = WebServer::last();
    CHECK(server != nullptr && server->begun);
    CHECK(WiFi.ssid_ == DASH_AP_SSID);
    CHECK(std::string(WiFi.softAPIP().toString().c_str()) == "192.168.4.1");

    server->testGet("/");
    CHECK(server->code == 200);
    CHECK(server->contentType == "text/html");
    CHECK(server->body.size() == DASH_PAGE_LEN);
    CHECK(server->body.find("LoRa Rangers") != std::string::npos);

    d.update(reading(1, 1, 1000), 500);
    d.update(reading(1, 2, 31000), 600);
    stubMillis() = 1000;
    server->testGet("/data", {{"since", "1"}});
    CHECK(server->code == 200);
    CHECK(server->contentType == "application/json");
    CHECK(count(server->body, "\"uid\":") == 1);
    saveJson("json_route", server->body.c_str());

    server->testGet("/nope");
    CHECK(server->code == 404);

    WebDashboard weak;
    CHECK(!weak.begin("x", "short")); // WPA2 password too short
}

int main(int argc, char **argv) {
    if (argc > 1) {
        g_jsonDir = argv[1];
    }
    struct {
        const char *name;
        void (*fn)();
    } tests[] = {
        {"valid CSV", testValidCsv},
        {"malformed CSV", testMalformedCsv},
        {"node ID 0, slots full", testBadNodeIds},
        {"MAC node IDs get slots", testSlotAssignment},
        {"gap", testGap},
        {"duplicate", testDuplicate},
        {"old packet", testOldPacket},
        {"seq wrap 65535 -> 0", testSeqWrap},
        {"node reboot", testReboot},
        {"OK -> LATE -> LOST timing", testStatusTiming},
        {"buildJson valid JSON", testJsonValid},
        {"since filter", testJsonSince},
        {"ring buffer", testRingBuffer},
        {"tiny buffers + paging", testJsonTinyBuffers},
        {"mode label, boot id, edge cases", testModeBootAndEdges},
        {"logLine", testLogLine},
        {"web routes", testRoutes},
    };
    for (auto &t : tests) {
        int before = g_fail;
        t.fn();
        std::printf("%s %s\n", g_fail == before ? "[PASS]" : "[FAIL]", t.name);
    }
    std::printf("\n%d checks passed, %d failed, %d JSON files written\n", g_pass,
                g_fail, g_jsonFiles);
    return g_fail == 0 ? 0 : 1;
}
