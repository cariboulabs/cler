#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "utils.hpp"
#include "desktop_blocks/ezgmsk/ezgmsk_demod.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/resamplers/multistage_resampler.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/sinks/sink_file.hpp"

constexpr const char* INPUT_FILE = "recordings/recorded_stream_0x55904E.bin";
constexpr const char* POST_DECIM_OUTPUT_FILE = "output/post_decim_output.bin";
constexpr const char* PREAMBLE_DETECTIONS_OUTPUT_FILE = "output/preamble_detections.bin";
constexpr const char* SYNCWORD_DETECTIONS_OUTPUT_FILE = "output/syncword_detections.bin";
constexpr const char* HEADER_DETECTIONS_OUTPUT_FILE = "output/header_detections.bin";
constexpr const char* PAYLOAD_DETECTIONS_OUTPUT_FILE = "output/payload_detections.bin";

constexpr size_t INPUT_SPS = 4000000;
constexpr size_t INPUT_BW = 160000; 
static_assert(INPUT_SPS  % INPUT_BW == 0, "Input MSPS must be a multiple of Input BW for decimation to work correctly.");

constexpr float BT = 0.3f;
constexpr size_t M = 3;
constexpr size_t N_INPUT_SAMPLES_PER_SYMBOL = INPUT_SPS / (200000/2); //bt is 0.3 + provided bw to ezlink
constexpr size_t N_DECIMATED_SAMPLES_PER_SYMBOL = 2;
constexpr float DECIM_RATIO = static_cast<float>(N_DECIMATED_SAMPLES_PER_SYMBOL) / static_cast<float>(N_INPUT_SAMPLES_PER_SYMBOL);
constexpr float DECIM_ATTENUATION = 80.0;

constexpr size_t PREAMBLE_SYMBOL_LEN = 24;
constexpr unsigned char SYNCWORD[] = {0x55, 0x90, 0x4E};
constexpr size_t HEADER_BYTE_LEN = 3;

struct CallbackContext {
    std::vector<unsigned int> preamble_detections;
    std::vector<unsigned int> syncword_detections;
    std::vector<unsigned int> header_detections;
    std::vector<unsigned int> payload_detections;
};

int ezgmsk_demod_cb(
            unsigned int _sample_counter,
            ezgmsk::ezgmsk_demod_state_en _state,
            [[maybe_unused]] unsigned char *  _header,
            [[maybe_unused]] unsigned char *  _payload,
            [[maybe_unused]] unsigned int     _payload_len,
            [[maybe_unused]] float            _rssi,
            [[maybe_unused]] float            _snr,
            [[maybe_unused]] void*            _context)
{
    static int preamble_counter = 0;
    static int syncword_counter = 0;
    static int header_counter = 0;
    static int payload_counter = 0;
    CallbackContext* callback_context = static_cast<CallbackContext*>(_context);

    if (_state == ezgmsk::EZGMSK_DEMOD_STATE_DETECTFRAME) {
        preamble_counter ++;
        callback_context->preamble_detections.push_back(_sample_counter);
    }

    else if (_state == ezgmsk::EZGMSK_DEMOD_STATE_RXSYNCWORD) {
        syncword_counter++;
        callback_context->syncword_detections.push_back(_sample_counter);
    }

    else if (_state == ezgmsk::EZGMSK_DEMOD_STATE_RXHEADER) {
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

    else if (_state == ezgmsk::EZGMSK_DEMOD_STATE_RXPAYLOAD) {
        payload_counter++;
        callback_context->payload_detections.push_back(_sample_counter);
    }

    return 0;
}

int main() {
    if (generate_output_directory() != 0) {return 1;}

    SourceFileBlock<std::complex<float>> input_file_block("Input File Block", INPUT_FILE, true);
    MultiStageResamplerBlock<std::complex<float>> decimator("Decimator",DECIM_RATIO, DECIM_ATTENUATION);
    FanoutBlock<std::complex<float>> fanout("Fanout Block", 2);

    SinkFileBlock<std::complex<float>> output_file_block("Output File Block", POST_DECIM_OUTPUT_FILE);

    size_t syncword_len = sizeof(SYNCWORD);
    size_t syncword_symbols_len = syncword_len * 8;
    unsigned char syncword_symbols[syncword_symbols_len];
    syncword_to_symbols(syncword_symbols, SYNCWORD, syncword_len);

    CallbackContext callback_context;
    EZGmskDemodBlock ezgmsk_demod("EZGMSK Demodulator",
                   N_DECIMATED_SAMPLES_PER_SYMBOL,
                   M,
                   BT,
                   PREAMBLE_SYMBOL_LEN,
                   syncword_symbols,
                   syncword_symbols_len,
                   HEADER_BYTE_LEN,
                   255, // Max payload length in bytes
                   ezgmsk_demod_cb,
                   &callback_context);


    cler::BlockRunner source_runner(&input_file_block, &decimator.in);
    cler::BlockRunner decimator_runner(&decimator, &fanout.in);
    cler::BlockRunner fanout_runner(&fanout, &ezgmsk_demod.in, &output_file_block.in);
    cler::BlockRunner ezgmsk_demod_runner(&ezgmsk_demod);
    cler::BlockRunner output_runner(&output_file_block);

    auto flowgraph = cler::make_desktop_flowgraph(
        source_runner,
        decimator_runner,
        fanout_runner,
        ezgmsk_demod_runner,
        output_runner
    );

    flowgraph.run_for(std::chrono::milliseconds(200));

    save_detections_to_file(PREAMBLE_DETECTIONS_OUTPUT_FILE, callback_context.preamble_detections);
    save_detections_to_file(SYNCWORD_DETECTIONS_OUTPUT_FILE, callback_context.syncword_detections);
    save_detections_to_file(HEADER_DETECTIONS_OUTPUT_FILE, callback_context.header_detections);
    save_detections_to_file(PAYLOAD_DETECTIONS_OUTPUT_FILE, callback_context.payload_detections);

    return 0;
}