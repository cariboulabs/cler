#pragma once

#include "cler.hpp"
#include "desktop_blocks/aprs/aprs.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/gui/map_canvas.hpp"
#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <string>

// APRS stations on the shared map, with a table. An origin (the receiver's own
// position) turns on a distance column.
struct APRSMapBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;

    cler::Channel<aprs::Packet> in;

    struct Station {
        char callsign[10] = {};
        char path[80] = {};
        char comment[64] = {};
        char type = 0;
        double lat = 0.0, lon = 0.0;
        bool has_position = false;
        float course = -1.0f, speed = -1.0f;
        bool has_altitude = false;
        int altitude_ft = 0;
        char symbol_table = 0, symbol_code = 0;
        uint32_t last_seen = 0;
        uint32_t packets = 0;
    };

    APRSMapBlock(const char* name, float center_lat = 32.8f, float center_lon = 35.0f,
                 bool have_origin = false,
                 const char* coastline_shp = "adsb_coastlines/ne_110m_coastline.shp")
        : cler::BlockBase(name), in(256), _have_origin(have_origin),
          _origin_lat(center_lat), _origin_lon(center_lon),
          _map(center_lat, center_lon, coastline_shp, 2.0f) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [rptr, rsize] = in.read_dbf();
        if (rsize == 0) return cler::Error::NotEnoughSamples;
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));
        {
            std::lock_guard<std::mutex> lock(_mutex);
            for (size_t i = 0; i < rsize; ++i) update(rptr[i], now);
        }
        in.commit_read(rsize);
        return cler::Empty{};
    }

    void render() {
        using namespace cler::palette;
        ImGui::SetNextWindowSize(_initial_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_pos, ImGuiCond_FirstUseEver);
        ImGui::Begin("APRS");
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        std::lock_guard<std::mutex> lock(_mutex);
        // table on the left
        ImGui::BeginChild("stations", ImVec2(400, 0), ImGuiChildFlags_Borders);
        ImGui::SeparatorText("Stations");
        ImGui::Text("%zu heard, %u packets", _stations.size(), _total);
        const int cols = _have_origin ? 4 : 3;
        if (ImGui::BeginTable("t", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, -70))) {
            ImGui::TableSetupColumn("callsign", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, 40);
            if (_have_origin) ImGui::TableSetupColumn("km", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableSetupColumn("age", ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();
            for (auto& [call, st] : _stations) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(st.callsign, _selected == call, ImGuiSelectableFlags_SpanAllColumns)) {
                    _selected = call;
                    if (st.has_position) {
                        _map.center_lat = static_cast<float>(st.lat);
                        _map.center_lon = static_cast<float>(st.lon);
                    }
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(type_name(st.type));
                if (_have_origin) {
                    ImGui::TableNextColumn();
                    if (st.has_position) ImGui::Text("%.1f", distance_km(st.lat, st.lon));
                }
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%us", now - st.last_seen);
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("Selected");
        auto it = _stations.find(_selected);
        if (it == _stations.end()) {
            ImGui::TextDisabled("no station selected");
        } else {
            const Station& st = it->second;
            ImGui::TextColored(accent_hi, "%s", st.callsign);
            if (st.path[0]) { ImGui::SameLine(); ImGui::TextDisabled("via %s", st.path); }
            if (st.speed >= 0.0f) ImGui::Text("%.0f kn  %.0f deg", st.speed, st.course < 0.0f ? 0.0f : st.course);
            if (st.has_altitude) {
                if (st.speed >= 0.0f) ImGui::SameLine(0, 12);   // a GGA compressed report has an altitude but no speed
                ImGui::Text("%d ft", st.altitude_ft);
            }
            ImGui::TextWrapped("%s", st.comment[0] ? st.comment : "");
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // map
        ImGui::BeginChild("map", ImVec2(0, 0));
        _map.begin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (_have_origin) {
            const ImVec2 o = _map.to_screen(_origin_lat, _origin_lon);
            dl->AddCircle(o, 6.0f, ImGui::GetColorU32(accent_hi), 0, 2.0f);
        }
        for (auto& [call, st] : _stations) {
            if (!st.has_position) continue;
            const ImVec2 p = _map.to_screen(static_cast<float>(st.lat), static_cast<float>(st.lon));
            const bool stale = now - st.last_seen > 1800;
            const ImU32 col = call == _selected ? ImGui::GetColorU32(accent_hi)
                            : stale ? ImGui::GetColorU32(faint)
                            : st.speed > 1.0f ? ImGui::GetColorU32(ok) : ImGui::GetColorU32(warn);
            if (st.speed > 1.0f && st.course >= 0.0f) _map.marker(dl, p, st.course, 7.0f, col);
            else dl->AddCircleFilled(p, 4.0f, col);
            dl->AddText(ImVec2(p.x + 9, p.y - 7), IM_COL32(255, 255, 255, 230), st.callsign);
        }
        ImGui::SetCursorScreenPos(ImVec2(_map.pos.x + 8, _map.pos.y + _map.size.y - 24));
        ImGui::TextDisabled("center %.3f, %.3f  zoom %.1fx  (drag to pan, wheel to zoom)", _map.center_lat, _map.center_lon, _map.zoom);
        _map.interact();
        ImGui::EndChild();
        ImGui::End();
    }

    void set_initial_window(float x, float y, float w, float h) {
        _initial_pos = ImVec2(x, y);
        _initial_size = ImVec2(w, h);
    }

    size_t station_count() const { std::lock_guard<std::mutex> lock(_mutex); return _stations.size(); }
    Station station(const char* callsign) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _stations.find(callsign);
        return it == _stations.end() ? Station{} : it->second;
    }

private:
    static const char* type_name(char t) {
        switch (t) {
            case '!': case '=': return "pos";
            case '@': case '/': return "pos+t";
            case '`': case '\'': case 0x1C: case 0x1D: return "Mic-E";
            case '>': return "status";
            case ':': return "msg";
            case ';': return "obj";
            case 'T': return "tlm";
            default: return "?";
        }
    }

    float distance_km(double lat, double lon) const {
        const double dlat = (lat - _origin_lat) * 111.32;
        const double dlon = (lon - _origin_lon) * 111.32 * std::cos(lat * M_PI / 180.0);
        return static_cast<float>(std::sqrt(dlat * dlat + dlon * dlon));
    }

    void update(const aprs::Packet& p, uint32_t now) {
        Station& st = _stations[p.source];
        if (_selected.empty()) _selected = p.source;
        std::memcpy(st.callsign, p.source, sizeof(st.callsign));
        std::memcpy(st.path, p.path, sizeof(st.path));
        st.type = p.type;
        st.last_seen = now;
        ++st.packets;
        ++_total;
        if (p.has_position) {
            st.lat = p.lat; st.lon = p.lon; st.has_position = true;
            st.symbol_table = p.symbol_table; st.symbol_code = p.symbol_code;
        }
        if (p.speed >= 0.0f) { st.speed = p.speed; st.course = p.course; }
        if (p.has_altitude) { st.has_altitude = true; st.altitude_ft = p.altitude_ft; }
        if (p.comment[0]) std::memcpy(st.comment, p.comment, sizeof(st.comment));
    }

    bool _have_origin;
    float _origin_lat, _origin_lon;
    mutable std::mutex _mutex;
    std::map<std::string, Station> _stations;
    std::string _selected;
    uint32_t _total = 0;
    MapCanvas _map;
    ImVec2 _initial_pos{0, 0}, _initial_size{1280, 720};
};
