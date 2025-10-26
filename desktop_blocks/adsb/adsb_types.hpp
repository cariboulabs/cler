#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>

// Aggregated aircraft state (unified across multiple Mode S messages)
struct ADSBState {
    uint32_t icao;                  // Aircraft ICAO address

    // Identification
    char callsign[9];               // Flight number (8 chars + null terminator)

    // Position (most recent valid)
    double lat;                     // Latitude (degrees)
    double lon;                     // Longitude (degrees)
    uint32_t position_update_time;  // Timestamp of last position update
    bool position_valid;            // True if lat/lon have been decoded

    // CPR position decoding state (for airborne positions)
    int last_even_cprlat;           // Last even frame latitude (17 bits)
    int last_even_cprlon;           // Last even frame longitude (17 bits)
    int last_odd_cprlat;            // Last odd frame latitude (17 bits)
    int last_odd_cprlon;            // Last odd frame longitude (17 bits)
    bool has_even_position;         // True if we have a valid even position
    bool has_odd_position;          // True if we have a valid odd position

    // Altitude and speed (most recent)
    int altitude;                   // Altitude (feet)
    float groundspeed;              // Ground speed (knots)
    float track;                    // Heading/track (degrees, 0-360)
    int vertical_rate;              // Vertical rate (feet/minute)

    // Metadata
    uint32_t last_update_time;      // Timestamp of last any update
    int message_count;              // Total messages received for this aircraft

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
