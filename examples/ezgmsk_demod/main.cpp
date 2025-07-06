#include "utils.hpp"
#include "_ezgmsk_demod.h"

constexpr char* INPUT_FILE = "recordings/recorded_stream_0x55904E.bin";
constexpr char* POST_DECIM_OUTPUT_FILE = "output/post_decim_output.bin";
constexpr char* PREAMBLE_DETECTIONS_OUTPUT_FILE = "output/preamble_detections.bin";
constexpr char* SYNCWORD_DETECTIONS_OUTPUT_FILE = "output/syncword_detections.bin";
constexpr char* HEADER_DETECTIONS_OUTPUT_FILE = "output/header_detections.bin";
constexpr char* PAYLOAD_DETECTIONS_OUTPUT_FILE = "output/payload_detections.bin";

constexpr size_t WORK_SIZE = 40; //How much to read from the input at once
constexpr size_t INPUT_MSPS = 4000000; // 4 MHz samples per second
constexpr size_t INPUT_BW = 160000.0; // 130 kHz bandwidth
static_assert(INPUT_MSPS  % INPUT_BW == 0, "Input MSPS must be a multiple of Input BW for decimation to work correctly.");

constexpr float BT = 0.3f;
constexpr size_t M = 3;
constexpr size_t N_INPUT_SAMPLES_PER_SYMBOL = INPUT_MSPS / (200000/2); //bt is 0.3 + config
constexpr size_t N_DECIMATED_SAMPLES_PER_SYMBOL = 2;
constexpr size_t DECIMATION_FACTOR = N_INPUT_SAMPLES_PER_SYMBOL / N_DECIMATED_SAMPLES_PER_SYMBOL;

constexpr float DECIM_ATTENUATION = 80.0;
constexpr float DECIM_FRAC = (float)1/DECIMATION_FACTOR;

constexpr float DETECTOR_THRESHOLD = 0.9;
constexpr float DETECTOR_DPHI_MAX = 0.1f; // Maximum carrier offset allowable

constexpr size_t PREAMBLE_LEN = 24; // Length of preamble in symbols
constexpr unsigned char SYNCWORD[] = {0x55, 0x90, 0x4E}; // Example syncword in bytes
constexpr size_t HEADER_BYTE_LEN = 3;


struct CallbackContext {
    std::vector<unsigned int> preamble_detections;
    std::vector<unsigned int> syncword_detections;
    std::vector<unsigned int> header_detections;
    std::vector<unsigned int> payload_detections;
};

int callback(
            unsigned int _sample_counter,
            ezgmsk_demod_state_en _state,
            unsigned char *  _header,
            unsigned char *  _payload,
            unsigned int     _payload_len,
            float            _rssi,
            float            _snr,
            void*            _context)
{
    static int preamble_counter = 0;
    static int syncword_counter = 0;
    static int header_counter = 0;
    static int payload_counter = 0;
    CallbackContext* callback_context = static_cast<CallbackContext*>(_context);

    if (_state == EZGMSK_DEMOD_STATE_DETECTFRAME) {
        preamble_counter ++;
        callback_context->preamble_detections.push_back(_sample_counter);
        return 0;
    }

    if (_state == EZGMSK_DEMOD_STATE_RXSYNCWORD) {
        syncword_counter++;
        callback_context->syncword_detections.push_back(_sample_counter);
        return 0;
    }

    if (_state == EZGMSK_DEMOD_STATE_RXHEADER) {
        header_counter++;
        callback_context->header_detections.push_back(_sample_counter);

        if (!_header) {
            std::cerr << "Header is null, cannot process header.\n";
            return 0;
        }
        uint16_t header = uint16_t(_header[0]) << 8 | uint16_t(_header[1]);
        uint8_t crc = EASYLINK_IEEE_HDR_GET_CRC(header);
        uint8_t whitening = EASYLINK_IEEE_HDR_GET_WHITENING(header);
        uint8_t length = EASYLINK_IEEE_HDR_GET_LENGTH(header);
        return length;
    }

    if (_state == EZGMSK_DEMOD_STATE_RXPAYLOAD) {
        payload_counter++;
        callback_context->payload_detections.push_back(_sample_counter);
        return 0;
    }
}

void syncword_to_symbols(unsigned char* out_symbols, const unsigned char* in_syncword, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        for (int bit = 7; bit >= 0; --bit) {
            out_symbols[i * 8 + (7 - bit)] = (in_syncword[i] >> bit) & 0x01;
        }
    }
}

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

    CallbackContext callback_context;

    ezgmsk_demod fs = ezgmsk_demod_create_set(
                                                N_DECIMATED_SAMPLES_PER_SYMBOL,
                                                M,
                                                BT,
                                                PREAMBLE_LEN,
                                                syncword_symbols,
                                                syncword_symbols_len,
                                                HEADER_BYTE_LEN,
                                                255, // Max payload length in bytes
                                                DETECTOR_THRESHOLD,
                                                DETECTOR_DPHI_MAX,
                                                callback,
                                                &callback_context);
    

    liquid_float_complex input_buffer[WORK_SIZE];
    liquid_float_complex post_decim_buffer[WORK_SIZE]; //it wont be bigger
    while (input_file.read(reinterpret_cast<char*>(input_buffer), WORK_SIZE * sizeof(liquid_float_complex))) {

        size_t bytes_read = input_file.gcount();
        size_t samples_read = bytes_read / sizeof(liquid_float_complex);

        unsigned int n_decimated_samples = 0;
        msresamp_crcf_execute(
            decimator,
            input_buffer, samples_read,
            post_decim_buffer, &n_decimated_samples
        );

        //write post decimated output to file
        for (size_t i = 0; i < n_decimated_samples; i++) {
            post_decim_output_file.write(reinterpret_cast<const char*>(&post_decim_buffer[i]), sizeof(liquid_float_complex));
        }

        ezgmsk_demod_execute(fs, post_decim_buffer, n_decimated_samples);
    }

    save_detections_to_file(PREAMBLE_DETECTIONS_OUTPUT_FILE, callback_context.preamble_detections);
    save_detections_to_file(SYNCWORD_DETECTIONS_OUTPUT_FILE, callback_context.syncword_detections);
    save_detections_to_file(HEADER_DETECTIONS_OUTPUT_FILE, callback_context.header_detections);
    save_detections_to_file(PAYLOAD_DETECTIONS_OUTPUT_FILE, callback_context.payload_detections);
}