#pragma once

#include "cler.hpp"
#include "desktop_blocks/ais/ais.hpp"
#include "desktop_blocks/gui/cler_palette.hpp"
#include "desktop_blocks/gui/map_canvas.hpp"
#include "imgui.h"

#include <cstdio>
#include <ctime>
#include <map>
#include <mutex>
#include <new>
#include <type_traits>

// Vessels from one or more AIS channels on the shared map, with a table.
struct AISMapBlock : public cler::BlockBase {
    static constexpr bool is_gui = true;
    static constexpr size_t MAX_INPUTS = 4;
    cler::Channel<ais::Message>* in;

    struct Vessel {
        uint32_t mmsi = 0;
        char name[21] = {};
        char callsign[8] = {};
        double lat = 0.0, lon = 0.0;
        bool has_position = false;
        float sog = -1.0f, cog = -1.0f;
        int heading = -1;
        int nav_status = -1;
        uint8_t ship_type = 0;
        uint32_t last_seen = 0;
        uint32_t messages = 0;
    };

    AISMapBlock(const char* name, size_t num_inputs = 1,
                float center_lat = 32.8f, float center_lon = 35.0f,
                const char* coastline_shp = "adsb_coastlines/ne_110m_coastline.shp")
        : cler::BlockBase(name), _num_inputs(num_inputs), _map(center_lat, center_lon, coastline_shp, 2.0f) {
        if (num_inputs == 0 || num_inputs > MAX_INPUTS) cler::panic("AISMapBlock: 1..4 inputs");
        in = reinterpret_cast<cler::Channel<ais::Message>*>(_in_storage);
        for (size_t i = 0; i < num_inputs; ++i) new (&in[i]) cler::Channel<ais::Message>(256);
    }

    ~AISMapBlock() {
        for (size_t i = 0; i < _num_inputs; ++i) in[i].~Channel();
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t total = 0;
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));
        std::lock_guard<std::mutex> lock(_mutex);
        for (size_t c = 0; c < _num_inputs; ++c) {
            auto [rptr, rsize] = in[c].read_dbf();
            for (size_t i = 0; i < rsize; ++i) update(rptr[i], now);
            in[c].commit_read(rsize);
            total += rsize;
        }
        return total ? cler::Result<cler::Empty, cler::Error>(cler::Empty{}) : cler::Error::NotEnoughSamples;
    }

    void render() {
        using namespace cler::palette;
        ImGui::SetNextWindowSize(_initial_size, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(_initial_pos, ImGuiCond_FirstUseEver);
        ImGui::Begin("AIS");
        const uint32_t now = static_cast<uint32_t>(std::time(nullptr));

        std::lock_guard<std::mutex> lock(_mutex);
        // table on the left
        ImGui::BeginChild("vessels", ImVec2(360, 0), ImGuiChildFlags_Borders);
        ImGui::Text("%zu vessels, %u messages", _vessels.size(), _total);
        if (ImGui::BeginTable("t", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("MMSI", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("name");
            ImGui::TableSetupColumn("kn", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("age", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableHeadersRow();
            for (auto& [mmsi, v] : _vessels) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char id[16];
                std::snprintf(id, sizeof(id), "%09u", mmsi);
                if (ImGui::Selectable(id, _selected == mmsi, ImGuiSelectableFlags_SpanAllColumns) && v.has_position) {
                    _selected = mmsi;
                    _map.center_lat = static_cast<float>(v.lat);
                    _map.center_lon = static_cast<float>(v.lon);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.name[0] ? v.name : "");
                ImGui::TableNextColumn();
                if (v.sog >= 0.0f) ImGui::Text("%.1f", v.sog);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%us", now - v.last_seen);
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // map
        ImGui::BeginChild("map", ImVec2(0, 0));
        _map.begin();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (auto& [mmsi, v] : _vessels) {
            if (!v.has_position) continue;
            const ImVec2 p = _map.to_screen(static_cast<float>(v.lat), static_cast<float>(v.lon));
            const bool stale = now - v.last_seen > 600;
            const ImU32 col = mmsi == _selected ? ImGui::GetColorU32(accent_hi)
                            : stale ? ImGui::GetColorU32(faint)
                            : v.sog > 0.5f ? ImGui::GetColorU32(ok) : ImGui::GetColorU32(warn);
            const float hdg = v.heading >= 0 ? static_cast<float>(v.heading) : v.cog >= 0.0f ? v.cog : 0.0f;
            if (v.sog > 0.5f || v.heading >= 0) _map.marker(dl, p, hdg, 7.0f, col);
            else dl->AddCircleFilled(p, 4.0f, col);
            if (v.name[0]) dl->AddText(ImVec2(p.x + 9, p.y - 7), IM_COL32(255, 255, 255, 230), v.name);
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

    size_t vessel_count() const { std::lock_guard<std::mutex> lock(_mutex); return _vessels.size(); }
    Vessel vessel(uint32_t mmsi) const {
        std::lock_guard<std::mutex> lock(_mutex);
        auto it = _vessels.find(mmsi);
        return it == _vessels.end() ? Vessel{} : it->second;
    }

private:
    void update(const ais::Message& m, uint32_t now) {
        Vessel& v = _vessels[m.mmsi];
        v.mmsi = m.mmsi;
        v.last_seen = now;
        ++v.messages;
        ++_total;
        if (m.has_position) { v.lat = m.lat; v.lon = m.lon; v.has_position = true; }
        if (m.type == 1 || m.type == 2 || m.type == 3 || m.type == 18) {
            v.sog = m.sog; v.cog = m.cog; v.heading = m.heading;
            if (m.type != 18) v.nav_status = m.nav_status;
        }
        if (m.name[0]) std::memcpy(v.name, m.name, sizeof(v.name));
        if (m.callsign[0]) std::memcpy(v.callsign, m.callsign, sizeof(v.callsign));
        if (m.ship_type) v.ship_type = m.ship_type;
    }

    size_t _num_inputs;
    std::aligned_storage_t<sizeof(cler::Channel<ais::Message>), alignof(cler::Channel<ais::Message>)> _in_storage[MAX_INPUTS];
    mutable std::mutex _mutex;
    std::map<uint32_t, Vessel> _vessels;
    uint32_t _total = 0;
    uint32_t _selected = 0;
    MapCanvas _map;
    ImVec2 _initial_pos{0, 0}, _initial_size{1280, 720};
};
