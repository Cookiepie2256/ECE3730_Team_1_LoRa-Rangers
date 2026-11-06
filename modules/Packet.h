
#include <cstdint>
class Packet {
    uint16_t node_id;
    uint16_t seq_num;
    int timestamp;
    double lat;
    double lng;
    int16_t temp;
    uint16_t humidity;
    uint16_t battery_mv;
    uint8_t hop_limit;
    uint8_t checksum;
};
