#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>

// aggregated aircraft state, unified across multiple Mode S messages
struct ADSBState {
    uint32_t icao;

    char callsign[9];                // 8 chars + null terminator

    double lat;                      // degrees
    double lon;                      // degrees
    uint32_t position_update_time;
    bool position_valid;

    // CPR (Compact Position Reporting) needs one even + one odd frame to decode a position
    int last_even_cprlat;            // 17 bits
    int last_even_cprlon;            // 17 bits
    int last_odd_cprlat;             // 17 bits
    int last_odd_cprlon;             // 17 bits
    bool has_even_position;
    bool has_odd_position;

    int altitude;                    // feet
    float groundspeed;                // knots
    float track;                     // degrees, 0-360
    int vertical_rate;               // feet/minute

    uint32_t last_update_time;
    int message_count;

    ADSBState()
        : icao(0), lat(0.0), lon(0.0), position_update_time(0),
          position_valid(false),
          last_even_cprlat(0), last_even_cprlon(0),
          last_odd_cprlat(0), last_odd_cprlon(0),
          has_even_position(false), has_odd_position(false),
          altitude(0), groundspeed(0.0f), track(0.0f), vertical_rate(0),
          last_update_time(0), message_count(0) {
        callsign[0] = '\0';
    }
};
