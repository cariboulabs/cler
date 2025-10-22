#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <cmath>

/**
 * Simple shapefile (.shp) parser for Natural Earth coastlines
 *
 * Shapefiles store geometry data in binary format:
 * - Header (100 bytes)
 * - Records with geometry data
 *
 * Each record contains a record header and shape data.
 * For polylines (type 3), we extract coordinate sequences.
 */
struct CoastlineData {
    std::vector<std::vector<std::pair<float, float>>> polylines;  // List of polylines (each is lat/lon pairs)

    bool load_from_shapefile(const char* shp_path) {
        std::ifstream file(shp_path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        // Read file size
        file.seekg(0, std::ios::end);
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read shapefile header (100 bytes)
        uint8_t header[100];
        file.read(reinterpret_cast<char*>(header), 100);
        if (file.gcount() != 100) {
            return false;
        }

        // Parse records starting at byte 100
        size_t offset = 100;

        while (offset < static_cast<size_t>(file_size)) {
            if (offset + 8 > static_cast<size_t>(file_size)) break;  // Not enough for record header

            file.seekg(offset, std::ios::beg);

            // Record header (8 bytes)
            uint32_t record_num_be, record_len_be;
            file.read(reinterpret_cast<char*>(&record_num_be), 4);
            file.read(reinterpret_cast<char*>(&record_len_be), 4);

            // Convert from big-endian (shapefile uses big-endian)
            uint32_t record_len = swap_endian(record_len_be) * 2;  // Length in 16-bit words, convert to bytes

            if (offset + 8 + record_len > static_cast<size_t>(file_size)) break;

            // Read shape type (4 bytes, little-endian)
            uint32_t shape_type_le;
            file.read(reinterpret_cast<char*>(&shape_type_le), 4);
            uint32_t shape_type = shape_type_le;  // Already little-endian in file for shape data

            // Type 3 = PolyLine
            if (shape_type == 3) {
                parse_polyline(file, record_len);
            }

            offset += 8 + record_len;
        }

        file.close();
        return !polylines.empty();
    }

private:
    // Swap endianness (big-endian to little-endian)
    static uint32_t swap_endian(uint32_t val) {
        return ((val & 0x000000FFU) << 24) |
               ((val & 0x0000FF00U) << 8) |
               ((val & 0x00FF0000U) >> 8) |
               ((val & 0xFF000000U) >> 24);
    }

    static double swap_endian_double(double val) {
        uint64_t* ptr = reinterpret_cast<uint64_t*>(&val);
        uint64_t swapped = swap_endian_double_bits(*ptr);
        return *reinterpret_cast<double*>(&swapped);
    }

    static uint64_t swap_endian_double_bits(uint64_t val) {
        return ((val & 0x00000000000000FFULL) << 56) |
               ((val & 0x000000000000FF00ULL) << 40) |
               ((val & 0x0000000000FF0000ULL) << 24) |
               ((val & 0x00000000FF000000ULL) << 8) |
               ((val & 0x000000FF00000000ULL) >> 8) |
               ((val & 0x0000FF0000000000ULL) >> 24) |
               ((val & 0x00FF000000000000ULL) >> 40) |
               ((val & 0xFF00000000000000ULL) >> 56);
    }

    void parse_polyline(std::ifstream& file, uint32_t record_len) {
        // After shape type (4 bytes already read)

        // Bounding box (32 bytes: xmin, ymin, xmax, ymax as doubles)
        double xmin, ymin, xmax, ymax;
        file.read(reinterpret_cast<char*>(&xmin), 8);
        file.read(reinterpret_cast<char*>(&ymin), 8);
        file.read(reinterpret_cast<char*>(&xmax), 8);
        file.read(reinterpret_cast<char*>(&ymax), 8);

        // Number of parts and points
        uint32_t num_parts, num_points;
        file.read(reinterpret_cast<char*>(&num_parts), 4);
        file.read(reinterpret_cast<char*>(&num_points), 4);

        if (num_parts == 0 || num_points == 0) {
            // Skip to next record
            return;
        }

        // Read part indices (each part is a uint32)
        std::vector<uint32_t> part_indices(num_parts);
        file.read(reinterpret_cast<char*>(part_indices.data()), num_parts * 4);

        // Read all points and organize by parts
        for (uint32_t part_idx = 0; part_idx < num_parts; ++part_idx) {
            uint32_t start_point = part_indices[part_idx];
            uint32_t end_point = (part_idx + 1 < num_parts) ? part_indices[part_idx + 1] : num_points;

            std::vector<std::pair<float, float>> polyline;

            for (uint32_t i = start_point; i < end_point; ++i) {
                double lon, lat;
                file.read(reinterpret_cast<char*>(&lon), 8);
                file.read(reinterpret_cast<char*>(&lat), 8);
                polyline.push_back({static_cast<float>(lat), static_cast<float>(lon)});
            }

            if (!polyline.empty()) {
                polylines.push_back(polyline);
            }
        }
    }
};
