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
