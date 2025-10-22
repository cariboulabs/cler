#pragma once

#include "cler.hpp"
#include "adsb_types.hpp"
#include "adsb_coastline_loader.hpp"
#include "libmodes/mode-s.h"
#include <unordered_map>
#include <ctime>
#include <imgui.h>
#include <cmath>
#include <vector>

/**
 * ADSBAggregateBlock
 *
 * Aggregates individual Mode S messages into unified aircraft states.
 * Maintains a map of ICAO → ADSBState, updating with each received message.
 *
 * Input:  Channel<mode_s_msg>     (decoded messages from ADSBDecoderBlock)
 * Output: (none - sink block)
 *
 * Features:
 * - Callback on aircraft state updates
 * - Snapshot function get_aircraft() for safe external access
 * - Interactive map rendering with aircraft visualization
 */
struct ADSBAggregateBlock : public cler::BlockBase {
    cler::Channel<mode_s_msg> message_in;

    // Callback type: called when aircraft state is updated
    typedef void (*OnAircraftUpdateCallback)(const ADSBState&, void* context);

    /**
     * Create aggregate block
     *
     * @param name Block name
     * @param callback Optional callback fired on state updates
     * @param callback_context Optional context passed to callback
     */
    ADSBAggregateBlock(const char* name,
                       OnAircraftUpdateCallback callback = nullptr,
                       void* callback_context = nullptr,
                       const char* coastline_data_path = "desktop_blocks/adsb/data/ne_110m_coastline.shp")
        : BlockBase(name), message_in(1024),
          _callback(callback), _callback_context(callback_context),
          _map_center_lat(37.5f), _map_center_lon(-121.5f),
          _map_zoom(10.0f), _coastlines_loaded(false) {
        // Load coastline data
        _coastlines_loaded = _coastline_data.load_from_shapefile(coastline_data_path);
    }

    // Procedure: read messages, update aircraft states, invoke callbacks
    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available = message_in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Read all available messages into temporary buffer
        mode_s_msg* msg_buffer = new mode_s_msg[available];
        message_in.readN(msg_buffer, available);

        uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        // Process all messages
        for (size_t i = 0; i < available; ++i) {
            const mode_s_msg& msg = msg_buffer[i];

            // Get or create aircraft state for this ICAO
            uint32_t icao = (msg.aa1 << 16) | (msg.aa2 << 8) | msg.aa3;
            ADSBState& state = _aircraft[icao];

            if (state.icao == 0) {
                // First time seeing this aircraft
                state.icao = icao;
            }

            // Track update
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

            // Update altitude if present
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
            // TODO: Implement CPR decoding for actual lat/lon
            if (msg.msgtype == 17 && msg.metype >= 9 && msg.metype <= 18) {
                if (msg.raw_latitude >= 0 && msg.raw_longitude >= 0) {
                    state.position_update_time = now;
                }
            }

            // Always update last_update_time and message_count
            state.last_update_time = now;
            state.message_count++;

            // Fire callback if state changed
            if (state_changed && _callback) {
                _callback(state, _callback_context);
            }
        }

        delete[] msg_buffer;
        return cler::Empty{};
    }

    /**
     * Get snapshot of current aircraft states
     *
     * @param buf Pointer to buffer to fill
     * @param max_count Maximum number of aircraft to return
     * @return Actual number of aircraft returned
     */
    size_t get_aircraft(ADSBState* buf, size_t max_count) const {
        size_t count = 0;
        for (const auto& pair : _aircraft) {
            if (count >= max_count) break;
            buf[count++] = pair.second;
        }
        return count;
    }

    /**
     * Get aircraft count
     */
    size_t aircraft_count() const {
        return _aircraft.size();
    }

    /**
     * Interactive map rendering with aircraft visualization
     */
    void render() {
        ImGui::SetNextWindowSize(_initial_window_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_window_position, ImGuiCond_FirstUseEver);
        ImGui::Begin("ADSB Map");

        // Canvas setup
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (canvas_size.x < 200.0f) canvas_size.x = 200.0f;
        if (canvas_size.y < 200.0f) canvas_size.y = 200.0f;

        ImVec2 canvas_p1 = ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Draw background
        draw_list->AddRectFilled(canvas_pos, canvas_p1, IM_COL32(30, 40, 50, 255));
        draw_list->AddRect(canvas_pos, canvas_p1, IM_COL32(200, 200, 200, 255));

        // Draw coordinate grid
        draw_grid(draw_list, canvas_pos, canvas_size);

        // Draw coastlines if loaded
        draw_coastlines(draw_list, canvas_pos, canvas_size);

        // Draw aircraft
        draw_aircraft(draw_list, canvas_pos, canvas_size);

        // Draw info text
        ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x + 10, canvas_p1.y - 30));
        ImGui::Text("Aircraft: %zu | Center: %.2f°N, %.2f°W | Zoom: %.1fx",
                    _aircraft.size(), _map_center_lat, -_map_center_lon, _map_zoom);

        // Handle mouse interactions
        handle_map_interaction(canvas_pos, canvas_size);

        ImGui::End();
    }

    /**
     * Set initial window position and size
     */
    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    std::unordered_map<uint32_t, ADSBState> _aircraft;
    OnAircraftUpdateCallback _callback;
    void* _callback_context;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{1400.0f, 800.0f};

    // Map state
    float _map_center_lat;
    float _map_center_lon;
    float _map_zoom;

    /**
     * Convert lat/lon to screen coordinates
     * Simple equirectangular projection (good enough for small areas)
     */
    ImVec2 lat_lon_to_screen(float lat, float lon, ImVec2 canvas_pos, ImVec2 canvas_size) {
        // Map extent in degrees
        float lat_span = 2.0f / _map_zoom;  // ~2 degrees default
        float lon_span = lat_span * (canvas_size.x / canvas_size.y);

        float lat_min = _map_center_lat - lat_span / 2.0f;
        float lon_min = _map_center_lon - lon_span / 2.0f;

        // Normalize to [0, 1]
        float x_norm = (lon - lon_min) / lon_span;
        float y_norm = (lat - lat_min) / lat_span;

        // Clamp to canvas
        x_norm = std::max(0.0f, std::min(1.0f, x_norm));
        y_norm = std::max(0.0f, std::min(1.0f, y_norm));

        // Convert to screen coordinates (lat increases downward on screen)
        ImVec2 screen;
        screen.x = canvas_pos.x + x_norm * canvas_size.x;
        screen.y = canvas_pos.y + (1.0f - y_norm) * canvas_size.y;

        return screen;
    }

    /**
     * Draw latitude/longitude grid
     */
    void draw_grid(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        float lat_span = 2.0f / _map_zoom;
        float lon_span = lat_span * (canvas_size.x / canvas_size.y);

        float lat_min = _map_center_lat - lat_span / 2.0f;
        float lon_min = _map_center_lon - lon_span / 2.0f;

        // Draw grid lines every 0.1 degrees (or fewer if zoomed out)
        float grid_step = (lat_span > 1.0f) ? 0.5f : 0.1f;

        for (float lat = std::floor(lat_min / grid_step) * grid_step; lat < lat_min + lat_span; lat += grid_step) {
            ImVec2 p1 = lat_lon_to_screen(lat, lon_min, canvas_pos, canvas_size);
            ImVec2 p2 = lat_lon_to_screen(lat, lon_min + lon_span, canvas_pos, canvas_size);
            draw_list->AddLine(p1, p2, IM_COL32(100, 100, 120, 100), 0.5f);
        }

        for (float lon = std::floor(lon_min / grid_step) * grid_step; lon < lon_min + lon_span; lon += grid_step) {
            ImVec2 p1 = lat_lon_to_screen(lat_min, lon, canvas_pos, canvas_size);
            ImVec2 p2 = lat_lon_to_screen(lat_min + lat_span, lon, canvas_pos, canvas_size);
            draw_list->AddLine(p1, p2, IM_COL32(100, 100, 120, 100), 0.5f);
        }
    }

    /**
     * Draw coastlines from Natural Earth data
     */
    void draw_coastlines(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        if (!_coastlines_loaded || _coastline_data.polylines.empty()) {
            return;
        }

        ImU32 coastline_color = IM_COL32(100, 200, 100, 180);

        // Draw each polyline
        for (const auto& polyline : _coastline_data.polylines) {
            if (polyline.size() < 2) continue;

            // Convert coordinates and draw line segments
            for (size_t i = 0; i < polyline.size() - 1; ++i) {
                ImVec2 p1 = lat_lon_to_screen(polyline[i].first, polyline[i].second, canvas_pos, canvas_size);
                ImVec2 p2 = lat_lon_to_screen(polyline[i + 1].first, polyline[i + 1].second, canvas_pos, canvas_size);

                // Only draw if both points are roughly on screen (avoid massive lines off-canvas)
                if ((p1.x >= canvas_pos.x - 100 || p2.x >= canvas_pos.x - 100) &&
                    (p1.x < canvas_pos.x + canvas_size.x + 100 || p2.x < canvas_pos.x + canvas_size.x + 100) &&
                    (p1.y >= canvas_pos.y - 100 || p2.y >= canvas_pos.y - 100) &&
                    (p1.y < canvas_pos.y + canvas_size.y + 100 || p2.y < canvas_pos.y + canvas_size.y + 100)) {
                    draw_list->AddLine(p1, p2, coastline_color, 1.5f);
                }
            }
        }
    }

    /**
     * Draw aircraft as colored triangles
     */
    void draw_aircraft(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        for (const auto& pair : _aircraft) {
            const ADSBState& state = pair.second;

            // Skip aircraft without valid position (for now, just draw at center)
            // TODO: Use actual lat/lon when CPR decoding is implemented
            ImVec2 pos = ImVec2(
                canvas_pos.x + canvas_size.x / 2.0f + (state.icao % 200 - 100),
                canvas_pos.y + canvas_size.y / 2.0f + ((state.icao >> 8) % 200 - 100)
            );

            // Color by altitude (blue=low, red=high)
            float alt_norm = std::min(1.0f, state.altitude / 40000.0f);
            ImU32 color = ImGui::GetColorU32(ImVec4(alt_norm, 0.5f, 1.0f - alt_norm, 1.0f));

            // Draw triangle pointing in direction of track
            float heading_rad = state.track * 3.14159f / 180.0f;
            float tri_size = 8.0f;

            ImVec2 v0(pos.x + tri_size * std::sin(heading_rad),
                      pos.y - tri_size * std::cos(heading_rad));
            ImVec2 v1(pos.x - tri_size * std::cos(heading_rad + 0.5f),
                      pos.y - tri_size * std::sin(heading_rad + 0.5f));
            ImVec2 v2(pos.x + tri_size * std::cos(heading_rad + 0.5f),
                      pos.y + tri_size * std::sin(heading_rad + 0.5f));

            draw_list->AddTriangleFilled(v0, v1, v2, color);
            draw_list->AddTriangle(v0, v1, v2, IM_COL32(255, 255, 255, 200), 1.0f);

            // Draw callsign label
            if (state.callsign[0] != '\0') {
                draw_list->AddText(ImVec2(pos.x + 10, pos.y - 8),
                                   IM_COL32(255, 255, 255, 255), state.callsign);
            }

            // Draw altitude
            char alt_str[16];
            snprintf(alt_str, sizeof(alt_str), "%d'", state.altitude);
            draw_list->AddText(ImVec2(pos.x + 10, pos.y + 4),
                               IM_COL32(200, 200, 200, 255), alt_str);
        }
    }

    /**
     * Handle mouse pan/zoom interactions
     */
    void handle_map_interaction(ImVec2 canvas_pos, ImVec2 canvas_size) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouse_pos = io.MousePos;

        // Check if mouse is over canvas
        bool mouse_over = mouse_pos.x >= canvas_pos.x && mouse_pos.x < canvas_pos.x + canvas_size.x &&
                          mouse_pos.y >= canvas_pos.y && mouse_pos.y < canvas_pos.y + canvas_size.y;

        if (mouse_over) {
            // Zoom with mouse wheel
            if (io.MouseWheel != 0.0f) {
                _map_zoom *= (1.0f + io.MouseWheel * 0.1f);
                _map_zoom = std::max(1.0f, std::min(50.0f, _map_zoom));
            }

            // Pan with right-click drag
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                float lat_span = 2.0f / _map_zoom;
                float lon_span = lat_span * (canvas_size.x / canvas_size.y);
                _map_center_lon += (delta.x / canvas_size.x) * lon_span;
                _map_center_lat -= (delta.y / canvas_size.y) * lat_span;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }
    }
};
