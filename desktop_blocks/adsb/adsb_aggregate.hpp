#pragma once

#include "cler.hpp"
#include "adsb_types.hpp"
#include "adsb_coastline_loader.hpp"
#include "modes.h"
#include <unordered_map>
#include <imgui.h>
#include <cmath>

struct ADSBAggregateBlock : public cler::BlockBase {
    cler::Channel<mode_s_msg> in;

    // Callback type: called when aircraft state is updated
    typedef void (*OnAircraftUpdateCallback)(const ADSBState&, void* context);

    ADSBAggregateBlock(const char* name,
                       float initial_map_center_lat = 32.0f,
                       float initial_map_center_lon = 34.0f,
                       OnAircraftUpdateCallback callback = nullptr,
                       void* callback_context = nullptr,
                       const char* coastline_data_path = "adsb_coastlines/ne_110m_coastline.shp")
        : BlockBase(name), in(1024),
          _callback(callback), _callback_context(callback_context),
          _map_center_lat(initial_map_center_lat), _map_center_lon(initial_map_center_lon),
          _map_zoom(0.1f), _coastlines_loaded(false) {
        // Load coastline data
        _coastlines_loaded = _coastline_data.load_from_shapefile(coastline_data_path);
    }

    // Procedure: read messages, update aircraft states, invoke callbacks
    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available = in.size();
        if (available == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t to_process = std::min(available, MESSAGE_BUFFER_SIZE);
        in.readN(_msg_buffer, to_process);

        uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        // Process all messages
        for (size_t i = 0; i < to_process; ++i) {
            const mode_s_msg& msg = _msg_buffer[i];

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

        // Add ImGui window flags to lock window in place (no move, no resize, no collapse)
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        ImGui::Begin("ADSB Map", nullptr, window_flags);

        // Canvas setup
        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        if (canvas_size.x < MIN_CANVAS_SIZE) {canvas_size.x = MIN_CANVAS_SIZE;}
        if (canvas_size.y < MIN_CANVAS_SIZE) {canvas_size.y = MIN_CANVAS_SIZE;}

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
        ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x + INFO_TEXT_OFFSET_X, canvas_p1.y - INFO_TEXT_OFFSET_Y));
        ImGui::Text("Aircraft: %zu | Center: %.2f°N, %.2f°W | Zoom: %.1fx",
                    _aircraft.size(), _map_center_lat, -_map_center_lon, _map_zoom);

        // Handle mouse interactions
        handle_map_interaction(canvas_pos, canvas_size);

        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_window_position = ImVec2(x, y);
        _initial_window_size = ImVec2(w, h);
    }

private:
    // Rendering constants
    static constexpr float CANVAS_BOUNDS_MARGIN = 100.0f;
    static constexpr float AIRCRAFT_SPREAD_RANGE = 200.0f;
    static constexpr float AIRCRAFT_SPREAD_OFFSET = 100.0f;
    static constexpr float TRIANGLE_SIZE = 8.0f;
    static constexpr float TRIANGLE_ANGLE_OFFSET = 0.5f;
    static constexpr float MAX_ALTITUDE_FOR_COLOR = 40000.0f;
    static constexpr float GRID_STEP_ZOOMED_OUT = 0.5f;
    static constexpr float GRID_STEP_ZOOMED_IN = 0.1f;
    static constexpr float GRID_ZOOM_THRESHOLD = 1.0f;
    static constexpr float COASTLINE_THICKNESS = 1.5f;
    static constexpr float MIN_CANVAS_SIZE = 200.0f;
    static constexpr float INFO_TEXT_OFFSET_X = 10.0f;
    static constexpr float INFO_TEXT_OFFSET_Y = 30.0f;
    static constexpr float LABEL_OFFSET_X = 10.0f;
    static constexpr float LABEL_OFFSET_Y_CALLSIGN = -8.0f;
    static constexpr float LABEL_OFFSET_Y_ALTITUDE = 4.0f;
    static constexpr float ZOOM_SENSITIVITY = 0.1f;
    static constexpr float MIN_ZOOM = 0.01f;
    static constexpr float MAX_ZOOM = 50.0f;
    static constexpr float DEFAULT_LAT_SPAN = 2.0f;
    static constexpr float INITIAL_WINDOW_SIZE_X = 1400.0f;
    static constexpr float INITIAL_WINDOW_SIZE_Y = 800.0f;

    std::unordered_map<uint32_t, ADSBState> _aircraft;
    OnAircraftUpdateCallback _callback;
    void* _callback_context;

    ImVec2 _initial_window_position{0.0f, 0.0f};
    ImVec2 _initial_window_size{INITIAL_WINDOW_SIZE_X, INITIAL_WINDOW_SIZE_Y};

    // Map state
    float _map_center_lat;
    float _map_center_lon;
    float _map_zoom;

    // Coastline data
    CoastlineData _coastline_data;
    bool _coastlines_loaded;

    static constexpr size_t MESSAGE_BUFFER_SIZE = 1024;
    mode_s_msg _msg_buffer[MESSAGE_BUFFER_SIZE];

    ImVec2 lat_lon_to_screen(float lat, float lon, ImVec2 canvas_pos, ImVec2 canvas_size) {
        // Map extent in degrees
        float lat_span = DEFAULT_LAT_SPAN / _map_zoom;
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

    void draw_grid(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        float lat_span = DEFAULT_LAT_SPAN / _map_zoom;
        float lon_span = lat_span * (canvas_size.x / canvas_size.y);

        float lat_min = _map_center_lat - lat_span / 2.0f;
        float lon_min = _map_center_lon - lon_span / 2.0f;

        // Draw grid lines every 0.1 degrees (or fewer if zoomed out)
        float grid_step = (lat_span > GRID_ZOOM_THRESHOLD) ? GRID_STEP_ZOOMED_OUT : GRID_STEP_ZOOMED_IN;

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
                if ((p1.x >= canvas_pos.x - CANVAS_BOUNDS_MARGIN || p2.x >= canvas_pos.x - CANVAS_BOUNDS_MARGIN) &&
                    (p1.x < canvas_pos.x + canvas_size.x + CANVAS_BOUNDS_MARGIN || p2.x < canvas_pos.x + canvas_size.x + CANVAS_BOUNDS_MARGIN) &&
                    (p1.y >= canvas_pos.y - CANVAS_BOUNDS_MARGIN || p2.y >= canvas_pos.y - CANVAS_BOUNDS_MARGIN) &&
                    (p1.y < canvas_pos.y + canvas_size.y + CANVAS_BOUNDS_MARGIN || p2.y < canvas_pos.y + canvas_size.y + CANVAS_BOUNDS_MARGIN)) {
                    draw_list->AddLine(p1, p2, coastline_color, COASTLINE_THICKNESS);
                }
            }
        }
    }

    void draw_aircraft(ImDrawList* draw_list, ImVec2 canvas_pos, ImVec2 canvas_size) {
        for (const auto& pair : _aircraft) {
            const ADSBState& state = pair.second;

            // Skip aircraft without valid position (for now, just draw at center)
            // TODO: Use actual lat/lon when CPR decoding is implemented
            ImVec2 pos = ImVec2(
                canvas_pos.x + canvas_size.x / 2.0f + (static_cast<float>(state.icao % static_cast<uint32_t>(AIRCRAFT_SPREAD_RANGE)) - AIRCRAFT_SPREAD_OFFSET),
                canvas_pos.y + canvas_size.y / 2.0f + (static_cast<float>((state.icao >> 8) % static_cast<uint32_t>(AIRCRAFT_SPREAD_RANGE)) - AIRCRAFT_SPREAD_OFFSET)
            );

            // Color by altitude (blue=low, red=high)
            float alt_norm = std::min(1.0f, state.altitude / MAX_ALTITUDE_FOR_COLOR);
            ImU32 color = ImGui::GetColorU32(ImVec4(alt_norm, 0.5f, 1.0f - alt_norm, 1.0f));

            // Draw triangle pointing in direction of track
            float heading_rad = state.track * 3.14159f / 180.0f;

            ImVec2 v0(pos.x + TRIANGLE_SIZE * std::sin(heading_rad),
                      pos.y - TRIANGLE_SIZE * std::cos(heading_rad));
            ImVec2 v1(pos.x - TRIANGLE_SIZE * std::cos(heading_rad + TRIANGLE_ANGLE_OFFSET),
                      pos.y - TRIANGLE_SIZE * std::sin(heading_rad + TRIANGLE_ANGLE_OFFSET));
            ImVec2 v2(pos.x + TRIANGLE_SIZE * std::cos(heading_rad + TRIANGLE_ANGLE_OFFSET),
                      pos.y + TRIANGLE_SIZE * std::sin(heading_rad + TRIANGLE_ANGLE_OFFSET));

            draw_list->AddTriangleFilled(v0, v1, v2, color);
            draw_list->AddTriangle(v0, v1, v2, IM_COL32(255, 255, 255, 200), 1.0f);

            // Draw callsign label
            if (state.callsign[0] != '\0') {
                draw_list->AddText(ImVec2(pos.x + LABEL_OFFSET_X, pos.y + LABEL_OFFSET_Y_CALLSIGN),
                                   IM_COL32(255, 255, 255, 255), state.callsign);
            }

            // Draw altitude
            char alt_str[16];
            snprintf(alt_str, sizeof(alt_str), "%d'", state.altitude);
            draw_list->AddText(ImVec2(pos.x + LABEL_OFFSET_X, pos.y + LABEL_OFFSET_Y_ALTITUDE),
                               IM_COL32(200, 200, 200, 255), alt_str);
        }
    }

    void handle_map_interaction(ImVec2 canvas_pos, ImVec2 canvas_size) {
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouse_pos = io.MousePos;

        // Check if mouse is over canvas
        bool mouse_over = mouse_pos.x >= canvas_pos.x && mouse_pos.x < canvas_pos.x + canvas_size.x &&
                          mouse_pos.y >= canvas_pos.y && mouse_pos.y < canvas_pos.y + canvas_size.y;

        if (mouse_over) {
            // Zoom with mouse wheel
            if (io.MouseWheel != 0.0f) {
                _map_zoom *= (1.0f + io.MouseWheel * ZOOM_SENSITIVITY);
                _map_zoom = std::max(MIN_ZOOM, std::min(MAX_ZOOM, _map_zoom));
            }

            // Pan with right-click drag
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
                ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                float lat_span = DEFAULT_LAT_SPAN / _map_zoom;
                float lon_span = lat_span * (canvas_size.x / canvas_size.y);
                _map_center_lon += (delta.x / canvas_size.x) * lon_span;
                _map_center_lat -= (delta.y / canvas_size.y) * lat_span;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
            }
        }
    }
};
