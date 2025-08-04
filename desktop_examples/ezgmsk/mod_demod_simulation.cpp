#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "utils.hpp"
#include "desktop_blocks/ezgmsk/ezgmsk_demod.hpp"
#include "desktop_blocks/ezgmsk/ezgmsk_mod.hpp"
#include "desktop_blocks/noise/awgn.hpp"

constexpr float BT = 0.3f;
constexpr size_t M = 3;
constexpr size_t SAMPLES_PER_SYMBOL = 2;
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
    if (_state == ezgmsk::EZGMSK_DEMOD_STATE_RXHEADER) {
        if (!_header) {
            std::cerr << "Header is null, cannot process header.\n";
            return 0;
        }
        
        uint32_t header =
            (static_cast<uint32_t>(_header[0]) << 16) |
            (static_cast<uint32_t>(_header[1]) << 8)  |
            (static_cast<uint32_t>(_header[2]) << 0);
        uint8_t crc = EASYLINK_IEEE_HDR_GET_CRC(header);
        uint8_t whitening = EASYLINK_IEEE_HDR_GET_WHITENING(header);
        uint8_t length = EASYLINK_IEEE_HDR_GET_LENGTH(header);
        return length;
    }

    if (_state == ezgmsk::EZGMSK_DEMOD_STATE_RXPAYLOAD) {
        printf("%s\n",_payload);
    }
    return 0;
}

struct BlobSource : public cler::BlockBase {
    BlobSource(const char* name,
        size_t max_blob_size,
        size_t num_slab_slots = 100)
        : cler::BlockBase(name), _slab(max_blob_size, num_slab_slots) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Blob>* out) {
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }
        for (size_t i = 0; i < out->space(); ++i) {
            auto result = _slab.take_slot();
            if (result.is_err()) {
                cler::Error err = result.unwrap_err();
                return err;
            }
            Blob blob = result.unwrap();

            std::string payload = "Blob data " + std::to_string(counter++);
            uint32_t header = EASYLINK_IEEE_HDR_CREATE(0, 0, static_cast<uint32_t>(payload.size())); //only need 24 bits..

            memcpy(blob.data, SYNCWORD, sizeof(SYNCWORD));
            uint8_t* header_dst = blob.data + sizeof(SYNCWORD);
            header_dst[0] = (header >> 16) & 0xFF;  // MSB
            header_dst[1] = (header >> 8)  & 0xFF;
            header_dst[2] = (header >> 0)  & 0xFF;  // LSB
            memcpy(blob.data + sizeof(SYNCWORD) + HEADER_BYTE_LEN, payload.data(), payload.size());
            blob.len = sizeof(SYNCWORD) + HEADER_BYTE_LEN + payload.size();
            out->push(blob);
        }

        return cler::Empty{};
    }

    private:
        Slab _slab;
        size_t counter = 0;
};

int main() {
    size_t syncword_len = sizeof(SYNCWORD);
    size_t syncword_symbols_len = syncword_len * 8;
    unsigned char syncword_symbols[syncword_symbols_len];
    syncword_to_symbols(syncword_symbols, SYNCWORD, syncword_len);

    BlobSource blob_source("Blob Source", 256, 100);

    EZGmskModBlock ezgmsk_mod("EZGMSK Modulator",
                SAMPLES_PER_SYMBOL,
                M,
                BT,
                PREAMBLE_SYMBOL_LEN,
                512); // Default buffer size

    NoiseAWGNBlock<std::complex<float>> noise_block("Noise Block",
                                       0.01f, 2 * 256 * sizeof(std::complex<float>));
    CallbackContext callback_context;
    EZGmskDemodBlock ezgmsk_demod("EZGMSK Demodulator",
                   SAMPLES_PER_SYMBOL,
                   M,
                   BT,
                   PREAMBLE_SYMBOL_LEN / 2, // Half the preamble length for detection
                   syncword_symbols,
                   syncword_symbols_len,
                   HEADER_BYTE_LEN,
                   255, // Max payload length in bytes
                   ezgmsk_demod_cb,
                   &callback_context);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&blob_source, &ezgmsk_mod.in),
        cler::BlockRunner(&ezgmsk_mod, &noise_block.in),
        cler::BlockRunner(&noise_block, &ezgmsk_demod.in),
        cler::BlockRunner(&ezgmsk_demod)
    );

    flowgraph.run_for(std::chrono::seconds(10));

    return 0;
}