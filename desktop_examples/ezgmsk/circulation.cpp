#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "utils.hpp"
#include "desktop_blocks/ezgmsk/ezgmsk_demod.hpp"
#include "desktop_blocks/ezgmsk/ezgmsk_mod.hpp"
#include "desktop_blocks/noise/awgn.hpp"


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
    std::atomic<bool> finished{false};
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
   
}

struct BlobSource : public cler::BlockBase {
    BlobSource(const char* name, size_t buffer_size = 512)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure() {

        return cler::Empty{};
    }
};

int main() {
    size_t syncword_len = sizeof(SYNCWORD);
    size_t syncword_symbols_len = syncword_len * 8;
    unsigned char syncword_symbols[syncword_symbols_len];
    syncword_to_symbols(syncword_symbols, SYNCWORD, syncword_len);

    EZGmskModBlock ezgmsk_mod("EZGMSK Modulator",
                N_DECIMATED_SAMPLES_PER_SYMBOL,
                M,
                BT,
                PREAMBLE_SYMBOL_LEN,
                512); // Default buffer size

    NoiseAWGNBlock<std::complex<float>> noise_block("Noise Block",
                                       0.001f);
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

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&ezgmsk_demod)
    );

    flowgraph.run();

    while (callback_context.finished == false) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    flowgraph.stop();

    return 0;
}