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