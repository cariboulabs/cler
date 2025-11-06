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
#include <numeric>

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

    cler::Channel<T>* in = nullptr;  // Array of input channels (like add.hpp)

    SinkUHDBlock(const char* name,
                 const std::string& args,
                 double freq,
                 double rate,
                 double gain = 0.0,
                 size_t num_channels = 1,
                 size_t channel_size = 0,
                 const std::string& otw_format = "sc16")
        : BlockBase(name),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          _num_channels(num_channels),
          wire_format(otw_format) {

        if (num_channels == 0) {
            throw std::invalid_argument("SinkUHDBlock: num_channels must be at least 1");
        }

        // Calculate buffer size for DBF
        size_t actual_buffer_size = (channel_size == 0) ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : channel_size;

        // Validate buffer size for DBF
        if (channel_size > 0 && channel_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            throw std::invalid_argument("Channel size too small for doubly-mapped buffers. Need at least " +
                std::to_string(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T)) + " elements");
        }

        // Allocate input channel array (like add.hpp pattern)
        in = static_cast<cler::Channel<T>*>(
            ::operator new[](num_channels * sizeof(cler::Channel<T>))
        );

        // Construct channels with placement new (first try/catch for channel construction)
        size_t constructed_channels = 0;
        try {
            for (size_t i = 0; i < num_channels; ++i) {
                new (&in[i]) cler::Channel<T>(actual_buffer_size);
                constructed_channels++;
            }
        } catch (...) {
            cleanup_channels(in, constructed_channels);
            in = nullptr;
            throw std::runtime_error("SinkUHDBlock: Failed to construct input channels");
        }

        // All remaining setup wrapped in comprehensive try/catch for exception safety
        // Any throw here will cleanup all channels automatically
        try {
            // Create USRP device
            usrp = uhd::usrp::multi_usrp::make(device_args);
            if (!usrp) {
                throw std::runtime_error("SinkUHDBlock: Failed to create USRP device with args: " + device_args);
            }

            // Verify device has enough channels
            if (num_channels > usrp->get_tx_num_channels()) {
                std::stringstream ss;
                ss << "SinkUHDBlock: Requested " << num_channels << " channels but device only has "
                   << usrp->get_tx_num_channels() << " TX channels";
                throw std::runtime_error(ss.str());
            }

            // Set thread priority for better performance
            uhd::set_thread_priority_safe(0.5, true);

            // Configure and validate all channels
            for (size_t ch = 0; ch < num_channels; ++ch) {
                // Validate and set sample rate
                usrp->set_tx_rate(sample_rate, ch);
                double actual_rate = usrp->get_tx_rate(ch);
                if (ch == 0 && std::abs(actual_rate - sample_rate) > 1.0) {
                    std::cout << "SinkUHDBlock: Requested rate " << sample_rate/1e6
                              << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
                    sample_rate = actual_rate;
                }

                // Validate and set center frequency
                auto freq_range = usrp->get_tx_freq_range(ch);
                if (center_freq < freq_range.start() || center_freq > freq_range.stop()) {
                    std::stringstream ss;
                    ss << "Frequency " << center_freq/1e6 << " MHz not supported on channel " << ch << ". "
                       << "Supported range: " << freq_range.start()/1e6 << "-"
                       << freq_range.stop()/1e6 << " MHz";
                    throw std::runtime_error(ss.str());
                }

                uhd::tune_request_t tune_req(center_freq);
                usrp->set_tx_freq(tune_req, ch);

                // Validate and set gain
                auto gain_range = usrp->get_tx_gain_range(ch);
                if (gain_db < gain_range.start() || gain_db > gain_range.stop()) {
                    std::stringstream ss;
                    ss << "Gain " << gain_db << " dB not supported on channel " << ch << ". "
                       << "Supported range: " << gain_range.start() << "-"
                       << gain_range.stop() << " dB";
                    throw std::runtime_error(ss.str());
                }
                usrp->set_tx_gain(gain_db, ch);
            }

            // Setup TX stream for all channels
            // CPU format: what the host sees (fc32, sc16, sc8)
            // OTW format: what goes over the wire (sc16, sc8, fc32, etc.)
            uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
            stream_args.channels.resize(num_channels);
            std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

            tx_stream = usrp->get_tx_stream(stream_args);
            if (!tx_stream) {
                throw std::runtime_error("SinkUHDBlock: Failed to setup TX stream");
            }

            // Allocate UHD buffer pointer array (vector auto-manages)
            _uhd_buffs.resize(num_channels);
            _read_ptrs.resize(num_channels);
            _read_sizes.resize(num_channels);

            // Get max number of samples per packet
            max_samps_per_packet = tx_stream->get_max_num_samps();

            // Print device info
            std::cout << "SinkUHDBlock: Initialized "
                      << usrp->get_mboard_name() << " / " << usrp->get_pp_string()
                      << std::endl;
            std::cout << "  Channels: " << num_channels << std::endl;
            std::cout << "  Frequency: " << center_freq/1e6 << " MHz (all channels)" << std::endl;
            std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS (all channels)" << std::endl;
            std::cout << "  Gain: " << gain_db << " dB (all channels)" << std::endl;
            std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
            std::cout << "  Max samples/packet: " << max_samps_per_packet << std::endl;

            // Print available antennas for first channel
            auto antennas = usrp->get_tx_antennas(0);
            if (!antennas.empty()) {
                std::cout << "  Available TX antennas: ";
                for (const auto& ant : antennas) {
                    std::cout << ant << " ";
                }
                std::cout << "(using: " << usrp->get_tx_antenna(0) << ")" << std::endl;
            }
        } catch (...) {
            // Cleanup channels on any exception during setup
            cleanup_channels(in, num_channels);
            in = nullptr;
            throw;  // Re-throw the original exception
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

        // Clean up input channel array
        if (in) {
            cleanup_channels(in, _num_channels);
        }

        // _uhd_buffs vector cleans up automatically

        // Print statistics
        if (underflow_count > 0) {
            std::cout << "SinkUHDBlock: Total underflows: " << underflow_count << std::endl;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        // Get DBF read pointers from all input channels
        for (size_t i = 0; i < _num_channels; ++i) {
            auto [ptr, size] = in[i].read_dbf();
            _read_ptrs[i] = ptr;
            _read_sizes[i] = size;

            if (_read_sizes[i] < 300) {
                return cler::Error::NotEnoughSamples;
            }

            uhd::tx_metadata_t md;
            md.start_of_burst = false;
            md.end_of_burst = false;
            md.has_time_spec = false;               

            size_t sent = tx_stream->send(_read_ptrs[i],
                            _read_sizes[i],
                            md,
                            0.1);  // 100ms timeout
            in->commit_read(sent);
        }

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

    // Control methods (per-channel)
    void set_frequency(double freq, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        uhd::tune_request_t tune_req(freq);
        usrp->set_tx_freq(tune_req, channel);
        if (channel == 0) {
            center_freq = freq;
        }
    }

    void set_gain(double gain, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_tx_gain(gain, channel);
        if (channel == 0) {
            gain_db = gain;
        }
    }

    void set_sample_rate(double rate, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_tx_rate(rate, channel);
        double actual_rate = usrp->get_tx_rate(channel);
        if (channel == 0) {
            sample_rate = actual_rate;
        }
    }

    void set_bandwidth(double bw, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_tx_bandwidth(bw, channel);
    }

    void set_antenna(const std::string& antenna, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        auto antennas = usrp->get_tx_antennas(channel);
        if (std::find(antennas.begin(), antennas.end(), antenna) == antennas.end()) {
            std::stringstream ss;
            ss << "Antenna '" << antenna << "' not supported on channel " << channel
               << ". Available antennas: ";
            for (const auto& ant : antennas) {
                ss << ant << " ";
            }
            throw std::runtime_error(ss.str());
        }
        usrp->set_tx_antenna(antenna, channel);
    }

    // Getters (channel 0 values cached for backward compatibility)
    double get_frequency(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return channel == 0 ? center_freq : usrp->get_tx_freq(channel);
    }

    double get_gain(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return channel == 0 ? gain_db : usrp->get_tx_gain(channel);
    }

    double get_sample_rate(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return channel == 0 ? sample_rate : usrp->get_tx_rate(channel);
    }

    double get_bandwidth(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_bandwidth(channel);
    }

    std::string get_antenna(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_antenna(channel);
    }

    std::vector<std::string> list_antennas(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_antennas(channel);
    }

    uhd::freq_range_t get_frequency_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_freq_range(channel);
    }

    uhd::gain_range_t get_gain_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_gain_range(channel);
    }

    std::vector<std::string> list_gains(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_gain_names(channel);
    }

    uhd::gain_range_t get_gain_range(const std::string& name, size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_gain_range(name, channel);
    }

    uhd::meta_range_t get_sample_rate_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_rates(channel);
    }

    uhd::freq_range_t get_bandwidth_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_bandwidth_range(channel);
    }

    // Get number of channels
    size_t get_num_channels() const { return _num_channels; }

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

    void set_frequency_timed(double freq, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        uhd::tune_request_t tune_req(freq);
        usrp->set_tx_freq(tune_req, channel);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
        if (channel == 0) {
            center_freq = freq;
        }
    }

    void set_gain_timed(double gain, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_tx_gain(gain, channel);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
        if (channel == 0) {
            gain_db = gain;
        }
    }

    void set_antenna_timed(const std::string& antenna, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_tx_antenna(antenna, channel);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
    }

    // GPIO Control (per-channel)
    void gpio_set_ctrl(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "CTRL", value, mask, channel);
    }

    void gpio_set_ddr(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "DDR", value, mask, channel);
    }

    void gpio_set_out(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "OUT", value, mask, channel);
    }

    uint32_t gpio_get_in(const std::string& bank, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_gpio_attr(bank, "READBACK", channel);
    }

    void gpio_set_out_timed(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "OUT", value, mask, channel);
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

    // Sensor Access (per-channel)
    std::vector<std::string> get_tx_sensor_names(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_sensor_names(channel);
    }

    std::string get_tx_sensor(const std::string& name, size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        return usrp->get_tx_sensor(name, channel).to_pp_string();
    }

    std::vector<std::string> get_mboard_sensor_names() const {
        return usrp->get_mboard_sensor_names();
    }

    std::string get_mboard_sensor(const std::string& name) const {
        return usrp->get_mboard_sensor(name).to_pp_string();
    }

    bool is_lo_locked(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SinkUHDBlock: Channel index out of range");
        }
        auto sensors = usrp->get_tx_sensor_names(channel);
        if (std::find(sensors.begin(), sensors.end(), "lo_locked") != sensors.end()) {
            return usrp->get_tx_sensor("lo_locked", channel).to_bool();
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
    // Exception-safe cleanup helper for channel array
    static void cleanup_channels(cler::Channel<T>* channels, size_t count) {
        if (channels) {
            using TChannel = cler::Channel<T>;
            for (size_t i = 0; i < count; ++i) {
                channels[i].~TChannel();
            }
            ::operator delete[](channels);
        }
    }

    // Configuration
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t _num_channels;
    std::string wire_format;  // OTW format (sc16, sc8, fc32, etc.)

    // Multi-channel support
    std::vector<void*> _uhd_buffs;  // Buffer pointers for UHD multi-channel send()
    std::vector<const T*> _read_ptrs;  // Temp storage for read_dbf pointers
    std::vector<size_t> _read_sizes;   // Temp storage for read_dbf sizes

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

// UHD operates on I/Q pairs - scalar types are not supported
using SinkUHDBlockCF32 = SinkUHDBlock<std::complex<float>>;
using SinkUHDBlockSC16 = SinkUHDBlock<std::complex<int16_t>>;
using SinkUHDBlockSC8 = SinkUHDBlock<std::complex<int8_t>>;
