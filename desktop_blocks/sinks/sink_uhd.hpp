#pragma once

#include "cler.hpp"

#ifdef __has_include
    #if __has_include(<uhd/usrp/multi_usrp.hpp>)
        #include <uhd/usrp/multi_usrp.hpp>
        #include <uhd/types/tune_request.hpp>
        #include <uhd/types/metadata.hpp>
        #include <uhd/utils/thread.hpp>
    #else
        #error "UHD headers not found. Please install libuhd-dev package."
    #endif
#endif

#include <complex>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <algorithm>

// Use format helper from source_uhd.hpp
template<typename T>
inline std::string get_uhd_format();  // Forward declaration

// TX Metadata structure for burst and timed transmissions
struct TxMetadata {
    bool has_time_spec = false;
    double time_seconds = 0.0;
    double time_frac_seconds = 0.0;
    bool start_of_burst = false;
    bool end_of_burst = false;
};

// Async TX Event structure
struct AsyncTxEvent {
    bool event_occurred = false;
    uhd::async_metadata_t::event_code_t event_code = uhd::async_metadata_t::EVENT_CODE_BURST_ACK;
    double time_seconds = 0.0;
    double time_frac_seconds = 0.0;
};

template<typename T>
struct SinkUHDBlock : public cler::BlockBase {

    cler::Channel<T> in;

    SinkUHDBlock(const char* name,
                 const std::string& args,
                 double freq,
                 double rate,
                 double gain = 0.0,
                 size_t channel = 0,
                 size_t channel_size = 0,
                 const std::string& otw_format = "sc16")
        : BlockBase(name),
          in(channel_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : channel_size),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          channel_idx(channel),
          wire_format(otw_format) {

        // Validate buffer size for DBF
        if (channel_size > 0 && channel_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Channel size too small for doubly-mapped buffers. Need at least " +
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements");
        }

        // Create USRP device
        usrp = uhd::usrp::multi_usrp::make(device_args);
        if (!usrp) {
            throw std::runtime_error("SinkUHDBlock: Failed to create USRP device with args: " + device_args);
        }

        // Set thread priority for better performance
        uhd::set_thread_priority_safe(0.5, true);

        // Validate and set sample rate
        auto sample_rates = usrp->get_tx_rates(channel_idx);
        double actual_rate = usrp->set_tx_rate(sample_rate, channel_idx);
        if (std::abs(actual_rate - sample_rate) > 1.0) {
            std::cout << "SinkUHDBlock: Requested rate " << sample_rate/1e6
                      << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
            sample_rate = actual_rate;
        }

        // Validate and set center frequency
        auto freq_range = usrp->get_tx_freq_range(channel_idx);
        if (center_freq < freq_range.start() || center_freq > freq_range.stop()) {
            std::stringstream ss;
            ss << "Frequency " << center_freq/1e6 << " MHz not supported. "
               << "Supported range: " << freq_range.start()/1e6 << "-"
               << freq_range.stop()/1e6 << " MHz";
            throw std::runtime_error(ss.str());
        }

        uhd::tune_request_t tune_req(center_freq);
        uhd::tune_result_t tune_result = usrp->set_tx_freq(tune_req, channel_idx);

        // Validate and set gain
        auto gain_range = usrp->get_tx_gain_range(channel_idx);
        if (gain_db < gain_range.start() || gain_db > gain_range.stop()) {
            std::stringstream ss;
            ss << "Gain " << gain_db << " dB not supported. "
               << "Supported range: " << gain_range.start() << "-"
               << gain_range.stop() << " dB";
            throw std::runtime_error(ss.str());
        }
        usrp->set_tx_gain(gain_db, channel_idx);

        // Setup TX stream
        // CPU format: what the host sees (fc32, sc16, sc8)
        // OTW format: what goes over the wire (sc16, sc8, fc32, etc.)
        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels = {channel_idx};
        tx_stream = usrp->get_tx_stream(stream_args);
        if (!tx_stream) {
            throw std::runtime_error("SinkUHDBlock: Failed to setup TX stream");
        }

        // Get max number of samples per packet
        max_samps_per_packet = tx_stream->get_max_num_samps();

        // Print device info
        std::cout << "SinkUHDBlock: Initialized "
                  << usrp->get_mboard_name() << " / " << usrp->get_pp_string()
                  << std::endl;
        std::cout << "  Frequency: " << center_freq/1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS" << std::endl;
        std::cout << "  Gain: " << gain_db << " dB" << std::endl;
        std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
        std::cout << "  Max samples/packet: " << max_samps_per_packet << std::endl;

        // Print available antennas
        auto antennas = usrp->get_tx_antennas(channel_idx);
        if (!antennas.empty()) {
            std::cout << "  Available TX antennas: ";
            for (const auto& ant : antennas) {
                std::cout << ant << " ";
            }
            std::cout << "(using: " << usrp->get_tx_antenna(channel_idx) << ")" << std::endl;
        }
    }

    ~SinkUHDBlock() {
        if (tx_stream) {
            // Send end-of-burst
            uhd::tx_metadata_t md;
            md.end_of_burst = true;
            try {
                tx_stream->send("", 0, md);
            } catch (...) {
                // Ignore errors during shutdown
            }
        }

        // Print statistics
        if (underflow_count > 0) {
            std::cout << "SinkUHDBlock: Total underflows: " << underflow_count << std::endl;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        // Use DBF for zero-copy read
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        // Process in MTU-sized chunks
        size_t samples_sent = 0;
        bool first_packet = true;

        while (samples_sent < read_size) {
            size_t to_send = std::min(max_samps_per_packet, read_size - samples_sent);
            bool last_packet = (samples_sent + to_send >= read_size);

            // Prepare metadata (use user-configured or default)
            uhd::tx_metadata_t md;
            md.has_time_spec = use_tx_metadata && next_tx_metadata.has_time_spec;
            if (md.has_time_spec) {
                md.time_spec = uhd::time_spec_t(
                    next_tx_metadata.time_seconds,
                    next_tx_metadata.time_frac_seconds
                );
            }

            // Handle burst flags
            if (use_tx_metadata) {
                md.start_of_burst = first_packet && next_tx_metadata.start_of_burst;
                md.end_of_burst = last_packet && next_tx_metadata.end_of_burst;
            } else {
                md.start_of_burst = false;
                md.end_of_burst = false;
            }

            // Transmit samples
            size_t num_tx = tx_stream->send(read_ptr + samples_sent, to_send, md, 0.1);  // 100ms timeout

            // CRITICAL FIX: After first packet with timed metadata, switch to continuous mode
            // This prevents:
            // 1. TIME_ERROR from stale timestamp reuse
            // 2. Spurious BURST_ACK from repeated start_of_burst flags
            // For "timed start, continuous stream" pattern, metadata is one-shot.
            // To send another timed burst, call set_tx_metadata() again.
            if (first_packet && use_tx_metadata && md.has_time_spec) {
                use_tx_metadata = false;  // Drop back to continuous streaming
            }

            first_packet = false;

            if (num_tx < to_send) {
                // Partial send - could be timeout or other issue
                samples_sent += num_tx;
                in.commit_read(samples_sent);
                handle_async_events();
                return cler::Error::NotEnoughSpace;
            }

            samples_sent += num_tx;
        }

        in.commit_read(samples_sent);

        // Check for async events
        handle_async_events();

        return cler::Empty{};
    }

    // Set metadata for next transmission
    void set_tx_metadata(const TxMetadata& md) {
        next_tx_metadata = md;
        use_tx_metadata = true;
    }

    // Clear TX metadata (return to continuous streaming mode)
    void clear_tx_metadata() {
        use_tx_metadata = false;
    }

    // Poll for async TX events (non-blocking)
    bool poll_async_event(AsyncTxEvent& event, double timeout = 0.0) {
        uhd::async_metadata_t async_md;
        if (tx_stream->recv_async_msg(async_md, timeout)) {
            event.event_occurred = true;
            event.event_code = async_md.event_code;
            event.time_seconds = async_md.time_spec.get_full_secs();
            event.time_frac_seconds = async_md.time_spec.get_frac_secs();
            return true;
        }
        event.event_occurred = false;
        return false;
    }

    // Control methods
    void set_frequency(double freq) {
        uhd::tune_request_t tune_req(freq);
        usrp->set_tx_freq(tune_req, channel_idx);
        center_freq = freq;
    }

    void set_gain(double gain) {
        usrp->set_tx_gain(gain, channel_idx);
        gain_db = gain;
    }

    void set_sample_rate(double rate) {
        double actual_rate = usrp->set_tx_rate(rate, channel_idx);
        sample_rate = actual_rate;
    }

    void set_bandwidth(double bw) {
        usrp->set_tx_bandwidth(bw, channel_idx);
    }

    void set_antenna(const std::string& antenna) {
        auto antennas = usrp->get_tx_antennas(channel_idx);
        if (std::find(antennas.begin(), antennas.end(), antenna) == antennas.end()) {
            std::stringstream ss;
            ss << "Antenna '" << antenna << "' not supported. Available antennas: ";
            for (const auto& ant : antennas) {
                ss << ant << " ";
            }
            throw std::runtime_error(ss.str());
        }
        usrp->set_tx_antenna(antenna, channel_idx);
    }

    // Getters
    double get_frequency() const { return center_freq; }
    double get_gain() const { return gain_db; }
    double get_sample_rate() const { return sample_rate; }

    double get_bandwidth() const {
        return usrp->get_tx_bandwidth(channel_idx);
    }

    std::string get_antenna() const {
        return usrp->get_tx_antenna(channel_idx);
    }

    std::vector<std::string> list_antennas() const {
        return usrp->get_tx_antennas(channel_idx);
    }

    uhd::freq_range_t get_frequency_range() const {
        return usrp->get_tx_freq_range(channel_idx);
    }

    uhd::gain_range_t get_gain_range() const {
        return usrp->get_tx_gain_range(channel_idx);
    }

    std::vector<std::string> list_gains() const {
        return usrp->get_tx_gain_names(channel_idx);
    }

    uhd::gain_range_t get_gain_range(const std::string& name) const {
        return usrp->get_tx_gain_range(name, channel_idx);
    }

    uhd::meta_range_t get_sample_rate_range() const {
        return usrp->get_tx_rates(channel_idx);
    }

    uhd::freq_range_t get_bandwidth_range() const {
        return usrp->get_tx_bandwidth_range(channel_idx);
    }

    // Device information
    std::string get_mboard_name() const {
        return usrp->get_mboard_name();
    }

    std::string get_pp_string() const {
        return usrp->get_pp_string();
    }

    // Statistics
    size_t get_underflow_count() const { return underflow_count; }
    void reset_underflow_count() { underflow_count = 0; }

    // ========== ADVANCED FEATURES ==========

    // Timed Commands (same as SourceUHDBlock)
    void set_command_time(double time_seconds, double frac_seconds = 0.0) {
        usrp->set_command_time(uhd::time_spec_t(time_seconds, frac_seconds));
        command_time_set = true;
    }

    void clear_command_time() {
        usrp->clear_command_time();
        command_time_set = false;
    }

    void set_frequency_timed(double freq) {
        uhd::tune_request_t tune_req(freq);
        usrp->set_tx_freq(tune_req, channel_idx);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
        center_freq = freq;
    }

    void set_gain_timed(double gain) {
        usrp->set_tx_gain(gain, channel_idx);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
        gain_db = gain;
    }

    void set_antenna_timed(const std::string& antenna) {
        usrp->set_tx_antenna(antenna, channel_idx);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
    }

    // GPIO Control
    void gpio_set_ctrl(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF) {
        usrp->set_gpio_attr(bank, "CTRL", value, mask, channel_idx);
    }

    void gpio_set_ddr(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF) {
        usrp->set_gpio_attr(bank, "DDR", value, mask, channel_idx);
    }

    void gpio_set_out(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF) {
        usrp->set_gpio_attr(bank, "OUT", value, mask, channel_idx);
    }

    uint32_t gpio_get_in(const std::string& bank) {
        return usrp->get_gpio_attr(bank, "READBACK", channel_idx);
    }

    void gpio_set_out_timed(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF) {
        usrp->set_gpio_attr(bank, "OUT", value, mask, channel_idx);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
    }

    // Time/Clock Synchronization
    void set_clock_source(const std::string& source) {
        usrp->set_clock_source(source);
    }

    void set_time_source(const std::string& source) {
        usrp->set_time_source(source);
    }

    void set_time_now(double seconds, double frac_seconds = 0.0) {
        usrp->set_time_now(uhd::time_spec_t(seconds, frac_seconds));
    }

    void set_time_next_pps(double seconds, double frac_seconds = 0.0) {
        usrp->set_time_next_pps(uhd::time_spec_t(seconds, frac_seconds));
    }

    void set_time_unknown_pps(double seconds, double frac_seconds = 0.0) {
        usrp->set_time_unknown_pps(uhd::time_spec_t(seconds, frac_seconds));
    }

    double get_time_now() const {
        return usrp->get_time_now().get_real_secs();
    }

    double get_time_last_pps() const {
        return usrp->get_time_last_pps().get_real_secs();
    }

    std::vector<std::string> get_clock_sources() const {
        return usrp->get_clock_sources(0);
    }

    std::vector<std::string> get_time_sources() const {
        return usrp->get_time_sources(0);
    }

    // Sensor Access
    std::vector<std::string> get_tx_sensor_names() const {
        return usrp->get_tx_sensor_names(channel_idx);
    }

    std::string get_tx_sensor(const std::string& name) const {
        return usrp->get_tx_sensor(name, channel_idx).to_pp_string();
    }

    std::vector<std::string> get_mboard_sensor_names() const {
        return usrp->get_mboard_sensor_names();
    }

    std::string get_mboard_sensor(const std::string& name) const {
        return usrp->get_mboard_sensor(name).to_pp_string();
    }

    bool is_lo_locked() const {
        auto sensors = usrp->get_tx_sensor_names(channel_idx);
        if (std::find(sensors.begin(), sensors.end(), "lo_locked") != sensors.end()) {
            return usrp->get_tx_sensor("lo_locked", channel_idx).to_bool();
        }
        return true;
    }

    bool is_ref_locked() const {
        auto sensors = usrp->get_mboard_sensor_names();
        if (std::find(sensors.begin(), sensors.end(), "ref_locked") != sensors.end()) {
            return usrp->get_mboard_sensor("ref_locked").to_bool();
        }
        return true;
    }

    void sync_all_devices() {
        std::cout << "Synchronizing USRP devices..." << std::endl;
        auto last_pps = usrp->get_time_last_pps();
        while (last_pps == usrp->get_time_last_pps()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        usrp->set_time_next_pps(uhd::time_spec_t(0.0));
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "USRP devices synchronized at t=0" << std::endl;
    }

protected:
    // Allow derived classes to access USRP
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::tx_streamer::sptr tx_stream;

private:
    void handle_async_events() {
        uhd::async_metadata_t async_md;
        while (tx_stream->recv_async_msg(async_md, 0.0)) {  // Non-blocking
            switch(async_md.event_code) {
                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW:
                case uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET:
                    underflow_count++;
                    if (underflow_count % 100 == 0) {
                        std::cerr << "SinkUHDBlock: Underflow count: " << underflow_count << std::endl;
                    }
                    break;

                case uhd::async_metadata_t::EVENT_CODE_TIME_ERROR:
                    std::cerr << "SinkUHDBlock: Time error - tried to send in the past" << std::endl;
                    break;

                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR:
                case uhd::async_metadata_t::EVENT_CODE_SEQ_ERROR_IN_BURST:
                    std::cerr << "SinkUHDBlock: Sequence error" << std::endl;
                    break;

                case uhd::async_metadata_t::EVENT_CODE_BURST_ACK:
                    // Burst acknowledged - normal
                    break;

                default:
                    break;
            }
        }
    }

private:
    // Configuration
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t channel_idx;
    std::string wire_format;  // OTW format (sc16, sc8, fc32, etc.)

    // Streaming
    size_t max_samps_per_packet;

    // TX Metadata
    TxMetadata next_tx_metadata;
    bool use_tx_metadata = false;

    // Timed command tracking
    bool command_time_set = false;

    // Statistics
    size_t underflow_count = 0;
};

// Common template instantiations (COMPLEX TYPES ONLY)
// UHD operates on I/Q pairs - scalar types are not supported
using SinkUHDBlockCF32 = SinkUHDBlock<std::complex<float>>;
using SinkUHDBlockSC16 = SinkUHDBlock<std::complex<int16_t>>;
using SinkUHDBlockSC8 = SinkUHDBlock<std::complex<int8_t>>;
