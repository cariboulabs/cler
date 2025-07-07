#include "BitSequence.hpp"
#include "liquid.h"
#include <unistd.h>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <complex>

int main() {
    //create output directory if it does not exist
    try {
        if (!std::filesystem::exists("output")) {
            std::filesystem::create_directory("output");
            std::cout << "Directory output created.\n";
        } else {
             for (const auto& entry : std::filesystem::directory_iterator("output")) {
                std::filesystem::remove_all(entry);  // removes files and subdirectories
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << '\n';
        return 1;
    }

    float bt = 0.3;
    constexpr size_t n_samples_per_symbol = 4;
    constexpr size_t n_symbols_filter_delay = 3;
    const BitSequence preamble = BitSequence(0x55555555); // Preamble bit sequence (32 bits)
    const BitSequence syncword = BitSequence(0xd391A6); // Syncword bit sequence (24 bits)

    printf("Preamble bit sequence: %s\n", preamble.into_string().c_str());
    printf("Syncword bit sequence: %s\n", syncword.into_string().c_str());

    std::ofstream reference_preamble_file("output/reference_preamble.bin", std::ios::binary);
    gmskmod modulator = gmskmod_create(n_samples_per_symbol, n_symbols_filter_delay, bt);
    liquid_float_complex reference_preamble[(preamble.length + n_symbols_filter_delay) * n_samples_per_symbol];
    gmskmod_reset(modulator);
    for (size_t i = 0; i < preamble.length + n_symbols_filter_delay; ++i) {
        gmskmod_modulate(modulator, preamble.get_bit(i % preamble.length), &reference_preamble[i * n_samples_per_symbol]);
        reference_preamble_file.write(reinterpret_cast<const char*>(&reference_preamble[i * n_samples_per_symbol]),
            n_samples_per_symbol * sizeof(liquid_float_complex));
    }
    reference_preamble_file.close();

    std::ofstream reference_syncword_file("output/reference_syncword.bin", std::ios::binary);
    liquid_float_complex reference_syncword[(syncword.length + n_symbols_filter_delay) * n_samples_per_symbol];
    gmskmod_reset(modulator);
    for (size_t i = 0; i < syncword.length + n_symbols_filter_delay; ++i) {
        gmskmod_modulate(modulator, syncword.get_bit(i % syncword.length), &reference_syncword[i * n_samples_per_symbol]);
        reference_syncword_file.write(reinterpret_cast<const char*>(&reference_syncword[i * n_samples_per_symbol]),
            n_samples_per_symbol * sizeof(liquid_float_complex));
    }
    reference_syncword_file.close(); 
}