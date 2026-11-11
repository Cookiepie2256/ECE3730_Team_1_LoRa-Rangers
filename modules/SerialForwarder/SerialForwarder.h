#ifndef SERIAL_FORWARDER_H
#define SERIAL_FORWARDER_H

#include <Arduino.h>
#include <PacketCodec.h>

// Sends decoded packets from the base station to the PC as one text line each:
//
//   [CSVDATA] node_id,seq,elapsed_ms,latitude,longitude,temp_c,humidity_pct,hops,battery_mv,rssi_dbm
//
// tools/serial_to_csv.py on the PC keeps only lines starting with "[CSVDATA] "
// and writes them to a CSV file, so other Serial prints (debug messages) are
// safe. If you change the prefix or fields here, update PREFIX and FIELDS in
// tools/serial_to_csv.py too.
class SerialForwarder {
  public:
    static const char *const PREFIX; // "[CSVDATA] " (includes trailing space)

    // Big enough for any line produced by formatLine()
    static const size_t MAX_LINE_LEN = 128;

    // `out` is usually Serial (already started with Serial.begin())
    explicit SerialForwarder(Print &out);

    // Writes one [CSVDATA] line for `pkt`. `rssiDbm` is the radio's signal
    // strength for this packet.
    bool forward(const DecodedPacket &pkt, int16_t rssiDbm);

    // Formats one [CSVDATA] line (with trailing newline) into `out`. Returns the
    // line length, or 0 if `out` is too small.
    static size_t formatLine(const DecodedPacket &pkt, int16_t rssiDbm,
                             char *out, size_t outLen);

  private:
    Print &out_;
};

#endif // SERIAL_FORWARDER_H
