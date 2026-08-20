// Packet link: random packets -> flexframe framing -> AWGN and a carrier offset
// -> deframing, printing the packet success rate over an SNR sweep. Headless.
//   ./packet_link [--packets <n>] [--bytes <n>] [--offset <Hz>] [--rate <S/s>]
//
// Each payload carries its own 2-byte index so a recovered packet can be scored
// against the one that was sent even when frames are lost out of order. Exit
// status is 0 when the noiseless point recovers every packet.
#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/fec/deframer.hpp"
#include "desktop_blocks/fec/framer.hpp"
#include "desktop_blocks/math/frequency_shift.hpp"
#include "desktop_blocks/noise/awgn.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

// Emits each packet once, then reports no progress so the graph goes idle.
struct PacketSourceBlock : public cler::BlockBase {
    PacketSourceBlock(const char* name, const std::vector<uint8_t>& packets, size_t packet_bytes)
        : cler::BlockBase(name), _packets(packets), _packet_bytes(packet_bytes) {}

    bool done() const { return _pos >= _packets.size(); }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<uint8_t>* out) {
        if (done() || out->space() < _packet_bytes) {
            return cler::Error::NotEnoughSpace;
        }
        out->writeN(_packets.data() + _pos, _packet_bytes);
        _pos += _packet_bytes;
        return cler::Empty{};
    }

private:
    const std::vector<uint8_t>& _packets;
    size_t _packet_bytes;
    size_t _pos = 0;
};

// Scores recovered packets against the transmitted set by their embedded index.
struct PacketScorerBlock : public cler::BlockBase {
    cler::Channel<uint8_t> in;

    PacketScorerBlock(const char* name, const std::vector<uint8_t>& packets, size_t packet_bytes,
                      size_t num_scored)
        : cler::BlockBase(name), in(8192), _packets(packets), _packet_bytes(packet_bytes),
          _correct(num_scored, false), _scratch(packet_bytes) {}

    size_t correct() const {
        size_t n = 0;
        for (bool c : _correct) n += c ? 1 : 0;
        return n;
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (in.size() < _packet_bytes) {
            return cler::Error::NotEnoughSamples;
        }
        in.readN(_scratch.data(), _packet_bytes);
        const size_t index = static_cast<size_t>(_scratch[0]) | (static_cast<size_t>(_scratch[1]) << 8);
        if (index < _correct.size() &&
            std::memcmp(_scratch.data(), _packets.data() + index * _packet_bytes, _packet_bytes) == 0) {
            _correct[index] = true;
        }
        return cler::Empty{};
    }

private:
    const std::vector<uint8_t>& _packets;
    size_t _packet_bytes;
    std::vector<bool> _correct;
    std::vector<uint8_t> _scratch;
};

// Mean sample power of one assembled frame, so the noise level can be quoted
// against the signal flexframegen actually produces.
static float frame_sample_power(size_t packet_bytes) {
    flexframegenprops_s props;
    flexframegenprops_init_default(&props);
    flexframegen fg = flexframegen_create(&props);
    std::vector<uint8_t> payload(packet_bytes, 0xA5);
    unsigned char header[8] = {0};
    flexframegen_assemble(fg, header, payload.data(), static_cast<unsigned int>(packet_bytes));
    const unsigned int n = flexframegen_getframelen(fg);
    std::vector<std::complex<float>> buf(n);
    flexframegen_write_samples(fg, buf.data(), n);
    flexframegen_destroy(fg);
    double acc = 0.0;
    for (const auto& s : buf) acc += std::norm(s);
    return static_cast<float>(acc / n);
}

// Fraction of the first `num_scored` packets recovered byte-exact at this SNR.
static double run_point(const std::vector<uint8_t>& packets, size_t packet_bytes, size_t num_scored,
                        float noise_stddev, double offset_hz, double sample_rate) {
    PacketSourceBlock source("source", packets, packet_bytes);
    PacketFramerBlock framer("framer", packet_bytes);
    NoiseAWGNBlock<std::complex<float>> awgn("awgn", noise_stddev, 8192);
    FrequencyShiftBlock shift("shift", offset_hz, sample_rate, 8192);
    PacketDeframerBlock deframer("deframer", packet_bytes);
    PacketScorerBlock scorer("scorer", packets, packet_bytes, num_scored);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &framer.in),
        cler::BlockRunner(&framer, &awgn.in),
        cler::BlockRunner(&awgn, &shift.in),
        cler::BlockRunner(&shift, &deframer.in),
        cler::BlockRunner(&deframer, &scorer.in),
        cler::BlockRunner(&scorer));

    fg.run();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (source.done() && framer.in.size() == 0 && awgn.in.size() == 0 &&
            shift.in.size() == 0 && deframer.in.size() == 0 && scorer.in.size() == 0) {
            break;
        }
    }
    // Frames land at the synchronizer a filter delay behind the samples that
    // carried them; give the last one time to close before scoring.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fg.stop();

    std::printf("  frames %u  headers %u  payloads %u  dropped %llu  evm %.1f dB  rssi %.1f dB\n",
                deframer.frames_detected(), deframer.headers_valid(), deframer.payloads_valid(),
                static_cast<unsigned long long>(deframer.payloads_dropped()),
                static_cast<double>(deframer.evm_db()), static_cast<double>(deframer.rssi_db()));
    return static_cast<double>(scorer.correct()) / static_cast<double>(num_scored);
}

int main(int argc, char** argv) {
    size_t num_packets = 100;
    size_t packet_bytes = 64;
    double offset_hz = 500.0;
    double sample_rate = 1e6;

    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--packets") num_packets = std::stoul(argv[i + 1]);
        else if (key == "--bytes") packet_bytes = std::stoul(argv[i + 1]);
        else if (key == "--offset") offset_hz = std::stod(argv[i + 1]);
        else if (key == "--rate") sample_rate = std::stod(argv[i + 1]);
        else { std::printf("unknown option %s\n", key.c_str()); return 1; }
    }
    if (packet_bytes < 4 || num_packets == 0 || num_packets > 65535) {
        std::printf("packet_bytes must be >= 4 and packets in 1..65535\n");
        return 1;
    }

    // Two trailing packets flush the last scored frame through the synchronizer.
    const size_t total_packets = num_packets + 2;
    std::mt19937 rng(12345);
    std::vector<uint8_t> packets(total_packets * packet_bytes);
    for (size_t p = 0; p < total_packets; ++p) {
        uint8_t* q = packets.data() + p * packet_bytes;
        q[0] = static_cast<uint8_t>(p & 0xFF);
        q[1] = static_cast<uint8_t>((p >> 8) & 0xFF);
        for (size_t i = 2; i < packet_bytes; ++i) q[i] = static_cast<uint8_t>(rng() & 0xFF);
    }

    const float signal_power = frame_sample_power(packet_bytes);
    const float snr_points[] = {0.0f, 3.0f, 6.0f};

    std::printf("packet_link: %zu packets x %zu bytes, offset %.0f Hz at %.0f S/s\n",
                num_packets, packet_bytes, offset_hz, sample_rate);

    std::printf("noiseless\n");
    const double clean = run_point(packets, packet_bytes, num_packets, 0.0f, offset_hz, sample_rate);
    std::printf("  success %.1f %%\n", 100.0 * clean);

    for (float snr_db : snr_points) {
        const float stddev = std::sqrt(signal_power / (2.0f * std::pow(10.0f, snr_db / 10.0f)));
        std::printf("SNR %.1f dB\n", static_cast<double>(snr_db));
        const double rate = run_point(packets, packet_bytes, num_packets, stddev, offset_hz, sample_rate);
        std::printf("  success %.1f %%\n", 100.0 * rate);
    }

    if (clean < 1.0) {
        std::printf("FAIL: noiseless link lost packets\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
