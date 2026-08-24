#pragma once

#include "desktop_blocks/ais/ais.hpp"
#include "desktop_blocks/aprs/aprs.hpp"
#include "desktop_blocks/fm/rds.hpp"
#include "desktop_blocks/web/proto.hpp"

#include <cmath>
#include <cstring>
#include <string>

// JsonTextSinkBlock finds these by ADL on JsonWriter; they live with the app
// because the field names are this receiver's wire contract, not the library's.
namespace web {

// off-air text is not guaranteed to be terminated
template <size_t N>
inline std::string field(const char (&s)[N]) { return std::string(s, strnlen(s, N)); }

// speeds and courses carry 0.1 resolution; a float widened to double prints ten
// digits of noise otherwise
inline double one_dp(float v) { return std::round(v * 10.0f) / 10.0; }

inline void to_json(const ais::Message& m, JsonWriter& w) {
    w.begin_obj().key("mmsi").num(m.mmsi).key("type").num(m.type);
    if (m.has_position) w.key("lat").num(m.lat).key("lon").num(m.lon);
    if (m.sog >= 0.0f) w.key("sog").num(one_dp(m.sog));
    if (m.cog >= 0.0f) w.key("cog").num(one_dp(m.cog));
    if (m.heading >= 0) w.key("heading").num(m.heading);
    if (m.nav_status >= 0) w.key("nav_status").num(m.nav_status);
    if (m.name[0]) w.key("name").str(field(m.name));
    if (m.callsign[0]) w.key("callsign").str(field(m.callsign));
    if (m.ship_type) w.key("ship_type").num(m.ship_type);
    w.end();
}

inline void to_json(const aprs::Packet& p, JsonWriter& w) {
    w.begin_obj().key("source").str(field(p.source)).key("dest").str(field(p.dest));
    if (p.path[0]) w.key("path").str(field(p.path));
    w.key("type").str(std::string(1, p.type ? p.type : '?'));
    if (p.has_position) w.key("lat").num(p.lat).key("lon").num(p.lon);
    if (p.course >= 0.0f) w.key("course").num(one_dp(p.course));
    if (p.speed >= 0.0f) w.key("speed").num(one_dp(p.speed));
    if (p.has_altitude) w.key("altitude_ft").num(p.altitude_ft);
    if (p.symbol_code) w.key("symbol").str(std::string(1, p.symbol_table) + p.symbol_code);
    if (p.comment[0]) w.key("comment").str(field(p.comment));
    w.end();
}

inline void to_json(const rds::Station& s, JsonWriter& w) {
    w.begin_obj().key("synced").boolean(s.synced).key("pi").num(s.pi).key("pty").num(s.pty)
     .key("tp").boolean(s.tp).key("ta").boolean(s.ta)
     .key("ps").str(field(s.ps)).key("rt").str(field(s.rt))
     .key("groups_ok").num(s.groups_ok)
     .key("corrected_pct").num(s.blocks_total ? 100.0 * s.blocks_corrected / s.blocks_total : 0.0)
     .key("bad_pct").num(s.blocks_total ? 100.0 * s.blocks_bad / s.blocks_total : 0.0)
     .end();
}

}
