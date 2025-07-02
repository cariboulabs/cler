#include "liquid.h"
#include "clgmskframesync.h"
#include "utils.hpp"
#include <iostream>
#include <fstream>

using liquid_complex = liquid_float_complex;


constexpr char* INPUT_FILE = "recordings/recorded_stream_0x55904E.bin";
constexpr char* POST_DECIM_OUTPUT_FILE = "output/post_decim_output.bin";

constexpr size_t WORK_SIZE = 1024; //How much to read from the input at once
constexpr size_t INPUT_MSPS = 4000000; // 4 MHz samples per second
constexpr size_t INPUT_BW = 160000.0; // 130 kHz bandwidth
static_assert(INPUT_MSPS  % INPUT_BW == 0, "Input MSPS must be a multiple of Input BW for decimation to work correctly.");

constexpr float BT = 0.3f;
constexpr size_t M = 3;
constexpr size_t N_INPUT_SAMPLES_PER_SYMBOL = INPUT_MSPS / (200000/2); //bt is 0.3 + config
constexpr size_t N_DECIMATED_SAMPLES_PER_SYMBOL = 2;
constexpr size_t DECIMATION_FACTOR = N_INPUT_SAMPLES_PER_SYMBOL / N_DECIMATED_SAMPLES_PER_SYMBOL; // 3

constexpr float DECIM_ATTENUATION = 80.0;
constexpr float DECIM_FRAC = (float)1/DECIMATION_FACTOR;

constexpr float DETECTOR_THRESHOLD = 0.5f;
constexpr float DETECTOR_DPHI_MAX = 0.05f; // Maximum carrier offset allowable

constexpr unsigned char PREAMBLE_LEN = 24; // Length of preamble in symbols
constexpr unsigned char SYNCWORD[] = {0x55, 0x90, 0x4E}; // Example syncword in bytes

int callback(clgmskframesync* _q)
{
    // if (_q->state == RX)
    // printf("***** gmskframesync callback invoked *****\n");
    return 0;
}

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

    
    size_t syncword_len = sizeof(SYNCWORD);
    size_t syncword_symbols_len = syncword_len * 8;
    unsigned char syncword_symbols[syncword_symbols_len];
    syncword_to_symbols(syncword_symbols, SYNCWORD, syncword_len);

    clgmskframesync fs = clgmskframesync_create_set(N_DECIMATED_SAMPLES_PER_SYMBOL,
                                                M,
                                                BT,
                                                PREAMBLE_LEN,
                                                syncword_symbols,
                                                syncword_symbols_len,
                                                DETECTOR_THRESHOLD,
                                                DETECTOR_DPHI_MAX,
                                                NULL,
                                                NULL);
    

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

        clgmskframesync_execute(fs, post_decim_buffer, n_decimated_samples);
    }
}