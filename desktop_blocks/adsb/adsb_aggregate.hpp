#pragma once

#include "cler.hpp"
#include "adsb_types.hpp"
#include "desktop_blocks/gui/map_canvas.hpp"
#include "modes.h"
#include "cpr.h"
#include <unordered_map>
#include <imgui.h>
#include <cmath>

struct ADSBAggregateBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    cler::Channel<mode_s_msg> in;

    typedef void (*OnAircraftUpdateCallback)(const ADSBState&, void* context);

    ADSBAggregateBlock(const char* name,
                       float initial_map_center_lat = 32.0f,
                       float initial_map_center_lon = 34.0f,
                       OnAircraftUpdateCallback callback = nullptr,
                       void* callback_context = nullptr,
                       const char* coastline_data_path = "adsb_coastlines/ne_110m_coastline.shp")
        : BlockBase(name), in(1024),
          _callback(callback), _callback_context(callback_context),
          _map(initial_map_center_lat, initial_map_center_lon, coastline_data_path) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (!read_ptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        for (size_t i = 0; i < read_size; ++i) {
            const mode_s_msg& msg = read_ptr[i];

            uint32_t icao = (msg.aa1 << 16) | (msg.aa2 << 8) | msg.aa3;
            ADSBState& state = _aircraft[icao];

            if (state.icao == 0) {
                state.icao = icao;
            }

            bool state_changed = false;

            // Update callsign if present (DF17 metype 1-4)
            if (msg.msgtype == 17 && msg.metype >= 1 && msg.metype <= 4) {
                if (msg.flight[0] != '\0') {
                    if (std::strncmp(state.callsign, msg.flight, 8) != 0) {
                        std::strncpy(state.callsign, msg.flight, 8);
                        state.callsign[8] = '\0';
                        state_changed = true;
                    }
                }
            }

            if (msg.altitude > 0) {
                if (state.altitude != msg.altitude) {
                    state.altitude = msg.altitude;
                    state_changed = true;
                }
            }

            // Update velocity if present (DF17 metype 19)
            if (msg.msgtype == 17 && msg.metype == 19) {
                if (msg.velocity > 0) {
                    if (static_cast<int>(state.groundspeed) != msg.velocity) {
                        state.groundspeed = static_cast<float>(msg.velocity);
                        state_changed = true;
                    }
                }
                if (msg.heading >= 0 && msg.heading <= 360) {
                    if (static_cast<int>(state.track) != msg.heading) {
                        state.track = static_cast<float>(msg.heading);
                        state_changed = true;
                    }
                }
                if (msg.vert_rate != 0) {
                    if (state.vertical_rate != msg.vert_rate) {
                        state.vertical_rate = msg.vert_rate;
                        state_changed = true;
                    }
                }
            }

            // Update position if present (DF17 metype 9-18)
            // CPR (Compact Position Reporting) requires both even and odd frames
            if (msg.msgtype == 17 && msg.metype >= 9 && msg.metype <= 18) {
                // Store the raw CPR values (msg.raw_latitude/longitude are unsigned 17-bit values)
                if (msg.fflag == 0) {  // Even frame
                    state.last_even_cprlat = msg.raw_latitude;
                    state.last_even_cprlon = msg.raw_longitude;
                    state.has_even_position = true;
                } else {  // Odd frame (fflag == 1)
                    state.last_odd_cprlat = msg.raw_latitude;
                    state.last_odd_cprlon = msg.raw_longitude;
                    state.has_odd_position = true;
                }

                if (state.has_even_position && state.has_odd_position) {
                    double lat, lon;
                    int result = decodeCPRairborne(
                        state.last_even_cprlat, state.last_even_cprlon,
                        state.last_odd_cprlat, state.last_odd_cprlon,
                        msg.fflag,
                        &lat, &lon
                    );

                    if (result == 0) {  // Success
                        state.lat = lat;
                        state.lon = lon;
                        state.position_valid = true;
                        state.position_update_time = now;
                        state_changed = true;
                    } else {
                        // CPR decode failed, reset for next attempt
                        state.has_even_position = false;
                        state.has_odd_position = false;
                    }
                }
            }

            state.last_update_time = now;
            state.message_count++;

            if (state_changed && _callback) {
                _callback(state, _callback_context);
            }
        }

        in.commit_read(read_size);
        return cler::Empty{};
    }

    size_t get_aircrafts(ADSBState* buf, size_t max_count) const {
        size_t count = 0;
        for (const auto& pair : _aircraft) {
            if (count >= max_count) break;
            buf[count++] = pair.second;
        }
        return count;
    }

    size_t aircraft_count() const {
        return _aircraft.size();
    }

    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);

        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        ImGui::Begin("ADSB Map", nullptr, window_flags);

        _map.begin();
        draw_aircraft(ImGui::GetWindowDrawList());

        ImGui::SetCursorScreenPos(ImVec2(_map.pos.x + INFO_TEXT_OFFSET_X, _map.pos.y + _map.size.y - INFO_TEXT_OFFSET_Y));
        const char latitude_hemisphere = _map.center_lat >= 0.0f ? 'N' : 'S';
        const char longitude_hemisphere = _map.center_lon >= 0.0f ? 'E' : 'W';
        ImGui::Text("Aircraft: %zu | Center: %.2f°%c, %.2f°%c | Zoom: %.1fx",
                    _aircraft.size(), std::fabs(_map.center_lat), latitude_hemisphere,
                    std::fabs(_map.center_lon), longitude_hemisphere, _map.zoom);

        _map.interact();
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    static constexpr float TRIANGLE_SIZE = 8.0f;
    static constexpr float MAX_ALTITUDE_FOR_COLOR = 40000.0f;
    static constexpr float INFO_TEXT_OFFSET_X = 10.0f;
    static constexpr float INFO_TEXT_OFFSET_Y = 30.0f;
    static constexpr float LABEL_OFFSET_X = 10.0f;
    static constexpr float LABEL_OFFSET_Y_CALLSIGN = -8.0f;
    static constexpr float INITIAL_WINDOW_SIZE_X = 1400.0f;
    static constexpr float INITIAL_WINDOW_SIZE_Y = 800.0f;

    std::unordered_map<uint32_t, ADSBState> _aircraft;
    OnAircraftUpdateCallback _callback;
    void* _callback_context;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{INITIAL_WINDOW_SIZE_X, INITIAL_WINDOW_SIZE_Y};


    MapCanvas _map;

    void draw_aircraft(ImDrawList* draw_list) {
        for (const auto& pair : _aircraft) {
            const ADSBState& state = pair.second;
            // no fallback: unpositioned aircraft are not drawn
            if (!state.position_valid) continue;
            const ImVec2 pos = _map.to_screen(static_cast<float>(state.lat), static_cast<float>(state.lon));
            const float alt_norm = std::min(1.0f, state.altitude / MAX_ALTITUDE_FOR_COLOR);
            _map.marker(draw_list, pos, state.track, TRIANGLE_SIZE,
                        ImGui::GetColorU32(ImVec4(alt_norm, 0.5f, 1.0f - alt_norm, 1.0f)));
            if (state.callsign[0] != '\0') {
                draw_list->AddText(ImVec2(pos.x + LABEL_OFFSET_X, pos.y + LABEL_OFFSET_Y_CALLSIGN),
                                   IM_COL32(255, 255, 255, 255), state.callsign);
            }
        }
    }
};
