#include <iostream>
#include <fstream>
#include <filesystem>

int generate_output_directory() {
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

void syncword_to_symbols(unsigned char* out_symbols, const unsigned char* in_syncword, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            out_symbols[i * 8 + (7 - bit)] = (in_syncword[i] >> bit) & 0x01;
        }
    }
}