#pragma once

#include "desktop_blocks/gui/coastline_loader.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>

// Shared lat/lon canvas for the ADS-B and AIS maps: equirectangular
// projection around a centre, grid, coastlines, wheel zoom and drag pan.
// Call begin() inside an ImGui window, draw markers with to_screen(), then
// interact().
struct MapCanvas {
    static constexpr float DEFAULT_LAT_SPAN = 2.0f;
    static constexpr float MIN_ZOOM = 0.01f, MAX_ZOOM = 50.0f, ZOOM_SENSITIVITY = 0.1f;
    static constexpr float MIN_CANVAS_SIZE = 200.0f, CANVAS_BOUNDS_MARGIN = 100.0f;

    float center_lat, center_lon, zoom;
    CoastlineData coastlines;
    bool coastlines_loaded = false;
    ImVec2 pos{0, 0}, size{0, 0};

    MapCanvas(float lat, float lon, const char* coastline_shp, float initial_zoom = 0.1f)
        : center_lat(lat), center_lon(lon), zoom(initial_zoom) {
        coastlines_loaded = coastlines.load_from_shapefile(coastline_shp);
    }

    // background, grid and coastlines over the remaining window area
    void begin() {
        pos = ImGui::GetCursorScreenPos();
        size = ImGui::GetContentRegionAvail();
        size.x = std::max(size.x, MIN_CANVAS_SIZE);
        size.y = std::max(size.y, MIN_CANVAS_SIZE);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p1(pos.x + size.x, pos.y + size.y);
        dl->AddRectFilled(pos, p1, IM_COL32(30, 40, 50, 255));
        dl->AddRect(pos, p1, IM_COL32(200, 200, 200, 255));
        draw_grid(dl);
        draw_coastlines(dl);
    }

    float lat_span() const { return DEFAULT_LAT_SPAN / zoom; }
    float lon_span() const { return lat_span() * (size.x / size.y); }

    ImVec2 to_screen(float lat, float lon) const {
        const float lat_min = center_lat - lat_span() / 2.0f;
        const float lon_min = center_lon - lon_span() / 2.0f;
        float x = std::clamp((lon - lon_min) / lon_span(), 0.0f, 1.0f);
        float y = std::clamp((lat - lat_min) / lat_span(), 0.0f, 1.0f);
        return ImVec2(pos.x + x * size.x, pos.y + (1.0f - y) * size.y);
    }

    bool on_screen(ImVec2 p) const {
        return p.x >= pos.x - CANVAS_BOUNDS_MARGIN && p.x < pos.x + size.x + CANVAS_BOUNDS_MARGIN &&
               p.y >= pos.y - CANVAS_BOUNDS_MARGIN && p.y < pos.y + size.y + CANVAS_BOUNDS_MARGIN;
    }

    bool mouse_over() const {
        const ImVec2 m = ImGui::GetIO().MousePos;
        return m.x >= pos.x && m.x < pos.x + size.x && m.y >= pos.y && m.y < pos.y + size.y;
    }

    void interact() {
        if (!mouse_over()) return;
        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) {
            zoom = std::clamp(zoom * (1.0f + io.MouseWheel * ZOOM_SENSITIVITY), MIN_ZOOM, MAX_ZOOM);
        }
        for (ImGuiMouseButton b : {ImGuiMouseButton_Left, ImGuiMouseButton_Right}) {
            if (!ImGui::IsMouseDragging(b, 0.0f)) continue;
            const ImVec2 d = ImGui::GetMouseDragDelta(b);
            center_lon -= (d.x / size.x) * lon_span();
            center_lat += (d.y / size.y) * lat_span();
            ImGui::ResetMouseDragDelta(b);
            break;
        }
    }

    // heading-pointing triangle (heading in degrees, 0 = north)
    void marker(ImDrawList* dl, ImVec2 p, float heading_deg, float s, ImU32 fill) const {
        const float a = heading_deg * 3.14159265f / 180.0f - 3.14159265f / 2.0f;
        const float c = std::cos(a), sn = std::sin(a);
        const ImVec2 v0(p.x + s * 1.2f * c, p.y + s * 1.2f * sn);
        const ImVec2 v1(p.x - s * 0.8f * c - s * 0.5f * sn, p.y - s * 0.8f * sn + s * 0.5f * c);
        const ImVec2 v2(p.x - s * 0.8f * c + s * 0.5f * sn, p.y - s * 0.8f * sn - s * 0.5f * c);
        dl->AddTriangleFilled(v0, v1, v2, fill);
        dl->AddTriangle(v0, v1, v2, IM_COL32(255, 255, 255, 200), 1.0f);
    }

private:
    void draw_grid(ImDrawList* dl) const {
        const float lat_min = center_lat - lat_span() / 2.0f, lon_min = center_lon - lon_span() / 2.0f;
        const float step = lat_span() > 1.0f ? 0.5f : 0.1f;
        for (float lat = std::floor(lat_min / step) * step; lat < lat_min + lat_span(); lat += step)
            dl->AddLine(to_screen(lat, lon_min), to_screen(lat, lon_min + lon_span()), IM_COL32(100, 100, 120, 100), 0.5f);
        for (float lon = std::floor(lon_min / step) * step; lon < lon_min + lon_span(); lon += step)
            dl->AddLine(to_screen(lat_min, lon), to_screen(lat_min + lat_span(), lon), IM_COL32(100, 100, 120, 100), 0.5f);
    }

    void draw_coastlines(ImDrawList* dl) const {
        if (!coastlines_loaded) return;
        for (const auto& line : coastlines.polylines) {
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                const ImVec2 a = to_screen(line[i].first, line[i].second), b = to_screen(line[i + 1].first, line[i + 1].second);
                if (on_screen(a) || on_screen(b)) dl->AddLine(a, b, IM_COL32(100, 200, 100, 180), 1.5f);
            }
        }
    }
};
