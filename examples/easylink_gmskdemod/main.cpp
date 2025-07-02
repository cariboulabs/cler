#include "liquid.h"
#include "clgmskframesync.h"
#include "utils.hpp"
#include <iostream>
#include <fstream>

using liquid_complex = liquid_float_complex;


constexpr char* INPUT_FILE = "recordings/recorded_stream_0xD391A6.bin";
constexpr char* POST_DECIM_OUTPUT_FILE = "output/post_decim_output.bin";

constexpr size_t WORK_SIZE = 1024; //How much to read from the input at once
constexpr size_t INPUT_MSPS = 4000000; // 4 MHz samples per second
constexpr size_t INPUT_BW = 130000; // 130 kHz bandwidth

constexpr float DECIM_ATTENUATION = 80.0;
constexpr float DECIM_FRAC = (float)INPUT_BW / (float)INPUT_MSPS;

// constexpr float BT = 0.3f;
// constexpr size_t M = 3;
// constexpr size_t bw = 200000; // 200 kHz bandwidth
// constexpr size_t symbols_per_second  = bw / 2; //becuse bt is 0.3
// constexpr size_t msps = 4000000; //4Mhz
// constexpr size_t n_full_samples_per_symbol = msps / symbols_per_second;
// constexpr size_t n_samples_per_symbol = n_full_samples_per_symbol / DECIMATION_FACTOR; // 500 samples per symbol after decimation
// constexpr unsigned char syncword[] = {0xD3, 0x91, 0xA6}; // Example syncword in bytes


int main() {
    if (generate_output_directory() != 0) {
        return 1;
    }
    std::ifstream input_file(INPUT_FILE, std::ios::binary);
    if (!input_file) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    std::ofstream post_decim_output_file(POST_DECIM_OUTPUT_FILE, std::ios::binary);

    msresamp_crcf decimator = msresamp_crcf_create(DECIM_FRAC, DECIM_ATTENUATION);

    liquid_complex input_buffer[WORK_SIZE];
    liquid_complex post_decim_buffer[WORK_SIZE]; //it wont be bigger
    while (input_file.read(reinterpret_cast<char*>(input_buffer), WORK_SIZE * sizeof(liquid_complex))) {

        size_t bytes_read = input_file.gcount();
        size_t samples_read = bytes_read / sizeof(liquid_complex);

        unsigned int n_decimated_samples = 0;
        msresamp_crcf_execute(
            decimator,
            input_buffer, samples_read,
            post_decim_buffer, &n_decimated_samples
        );

        //write post decimated output to file
        for (size_t i = 0; i < n_decimated_samples; i++) {
            post_decim_output_file.write(reinterpret_cast<const char*>(&post_decim_buffer[i]), sizeof(liquid_complex));
        }
    }
}