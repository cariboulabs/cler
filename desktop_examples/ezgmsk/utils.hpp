#pragma once

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>


#define EASYLINK_IEEE_HDR_CRC_S                     (12U)
#define EASYLINK_IEEE_HDR_WHTNG_S                   (11U)
#define EASYLINK_IEEE_HDR_LEN_S                     (0U)
#define EASYLINK_IEEE_HDR_LEN_M                     (0x00FFU)
#define EASYLINK_IEEE_HDR_GET_CRC(header) \
    (((header) >> EASYLINK_IEEE_HDR_CRC_S) & 0x1U)   // Assuming 1-bit CRC field
#define EASYLINK_IEEE_HDR_GET_WHITENING(header) \
    (((header) >> EASYLINK_IEEE_HDR_WHTNG_S) & 0x1U) // Assuming 1-bit Whitening field
#define EASYLINK_IEEE_HDR_GET_LENGTH(header) \
    (((header) >> EASYLINK_IEEE_HDR_LEN_S) & EASYLINK_IEEE_HDR_LEN_M)

#define EASYLINK_IEEE_HDR_CREATE(crc, whitening, length) {                         \
    ((crc << EASYLINK_IEEE_HDR_CRC_S) | (whitening << EASYLINK_IEEE_HDR_WHTNG_S) | \
    ((length << EASYLINK_IEEE_HDR_LEN_S) & EASYLINK_IEEE_HDR_LEN_M))               \
}


inline void save_detections_to_file(const std::string& filename, const std::vector<unsigned int>& detections) {
    std::ofstream outfile(filename, std::ios::binary);
    if (!outfile) {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }
    if (!detections.empty()) {
        outfile.write(
            reinterpret_cast<const char*>(detections.data()),
            detections.size() * sizeof(unsigned int)
        );
    }
    outfile.close();
    if (!outfile) {
        std::cerr << "Error writing to output file: " << filename << "\n";
    } else {
        std::cout << "Detections saved to: " << filename << "\n";
    }
}

inline int generate_output_directory() {
    try {
        if (!std::filesystem::exists("output")) {
            std::filesystem::create_directory("output");
            std::cout << "output directory created.\n";
        } else {
             for (const auto& entry : std::filesystem::directory_iterator("output")) {
                std::filesystem::remove_all(entry);  // removes files and subdirectories
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}

inline void syncword_to_symbols(unsigned char* out_symbols, const unsigned char* in_syncword, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            out_symbols[i * 8 + (7 - bit)] = (in_syncword[i] >> bit) & 0x01;
        }
    }
}

