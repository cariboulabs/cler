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

// Helper to map C++ types to UHD CPU format strings
// NOTE: UHD fundamentally operates on I/Q pairs. Scalar types are not supported.
template<typename T>
inline std::string get_uhd_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return "fc32";
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return "sc16";
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return "sc8";
    } else {
        static_assert(!std::is_same_v<T, T>,
            "UHD blocks only support complex types (complex<float>, complex<int16_t>, complex<int8_t>). "
            "UHD operates on I/Q pairs - scalar types would cause stride mismatch and garbage data.");
    }
}

// RX Metadata structure for timestamp information
struct RxMetadata {
    bool has_time_spec = false;
    double time_seconds = 0.0;
    double time_frac_seconds = 0.0;
    bool start_of_burst = false;
    bool end_of_burst = false;
    bool more_fragments = false;
    uhd::rx_metadata_t::error_code_t error_code = uhd::rx_metadata_t::ERROR_CODE_NONE;
};

template<typename T>
struct SourceUHDBlock : public cler::BlockBase {

    SourceUHDBlock(const char* name,
                   const std::string& args,
                   double freq,
                   double rate,
                   double gain = 20.0,
                   size_t num_channels = 1,
                   const std::string& otw_format = "sc16")
        : BlockBase(name),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          _num_channels(num_channels),
          wire_format(otw_format) {

        if (num_channels == 0) {
            throw std::invalid_argument("SourceUHDBlock: num_channels must be at least 1");
        }

        // Create USRP device
        usrp = uhd::usrp::multi_usrp::make(device_args);
        if (!usrp) {
            throw std::runtime_error("SourceUHDBlock: Failed to create USRP device with args: " + device_args);
        }

        // Verify device has enough channels
        if (num_channels > usrp->get_rx_num_channels()) {
            std::stringstream ss;
            ss << "SourceUHDBlock: Requested " << num_channels << " channels but device only has "
               << usrp->get_rx_num_channels() << " RX channels";
            throw std::runtime_error(ss.str());
        }

        // Set thread priority for better performance
        uhd::set_thread_priority_safe(0.5, true);

        // Configure and validate all channels BEFORE allocating resources
        // This ensures exception safety - if validation throws, no leaks occur
        for (size_t ch = 0; ch < num_channels; ++ch) {
            // Validate and set sample rate
            usrp->set_rx_rate(sample_rate, ch);
            double actual_rate = usrp->get_rx_rate(ch);
            if (ch == 0 && std::abs(actual_rate - sample_rate) > 1.0) {
                std::cout << "SourceUHDBlock: Requested rate " << sample_rate/1e6
                          << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
                sample_rate = actual_rate;
            }

            // Validate and set center frequency
            auto freq_range = usrp->get_rx_freq_range(ch);
            if (center_freq < freq_range.start() || center_freq > freq_range.stop()) {
                std::stringstream ss;
                ss << "Frequency " << center_freq/1e6 << " MHz not supported on channel " << ch << ". "
                   << "Supported range: " << freq_range.start()/1e6 << "-"
                   << freq_range.stop()/1e6 << " MHz";
                throw std::runtime_error(ss.str());
            }

            uhd::tune_request_t tune_req(center_freq);
            usrp->set_rx_freq(tune_req, ch);

            // Validate and set gain
            auto gain_range = usrp->get_rx_gain_range(ch);
            if (gain_db < gain_range.start() || gain_db > gain_range.stop()) {
                std::stringstream ss;
                ss << "Gain " << gain_db << " dB not supported on channel " << ch << ". "
                   << "Supported range: " << gain_range.start() << "-"
                   << gain_range.stop() << " dB";
                throw std::runtime_error(ss.str());
            }
            usrp->set_rx_gain(gain_db, ch);
        }

        // Setup RX stream for all channels (do this BEFORE allocating _uhd_buffs)
        // CPU format: what the host sees (fc32, sc16, sc8)
        // OTW format: what goes over the wire (sc16, sc8, fc32, etc.)
        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels.resize(num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

        rx_stream = usrp->get_rx_stream(stream_args);
        if (!rx_stream) {
            throw std::runtime_error("SourceUHDBlock: Failed to setup RX stream");
        }

        // Get max number of samples per packet
        max_samps_per_packet = rx_stream->get_max_num_samps();

        // Start continuous streaming
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);

        // Allocate UHD buffer pointer array (vector auto-manages, exception-safe)
        _uhd_buffs.resize(num_channels);
        _write_ptrs.resize(num_channels);
        _write_sizes.resize(num_channels);

        // Print device info
        std::cout << "SourceUHDBlock: Initialized "
                  << usrp->get_mboard_name() << " / " << usrp->get_pp_string()
                  << std::endl;
        std::cout << "  Channels: " << num_channels << std::endl;
        std::cout << "  Frequency: " << center_freq/1e6 << " MHz (all channels)" << std::endl;
        std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS (all channels)" << std::endl;
        std::cout << "  Gain: " << gain_db << " dB (all channels)" << std::endl;
        std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
        std::cout << "  Max samples/packet: " << max_samps_per_packet << std::endl;

        // Print available antennas for first channel
        auto antennas = usrp->get_rx_antennas(0);
        if (!antennas.empty()) {
            std::cout << "  Available RX antennas: ";
            for (const auto& ant : antennas) {
                std::cout << ant << " ";
            }
            std::cout << "(using: " << usrp->get_rx_antenna(0) << ")" << std::endl;
        }
    }

    ~SourceUHDBlock() {
        if (rx_stream) {
            // Stop streaming
            uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            stream_cmd.stream_now = true;
            try {
                rx_stream->issue_stream_cmd(stream_cmd);
            } catch (...) {
                // Ignore errors during shutdown
            }
        }

        // _uhd_buffs vector cleans up automatically

        // Print statistics
        if (overflow_count > 0) {
            std::cout << "SourceUHDBlock: Total overflows: " << overflow_count << std::endl;
        }
    }

    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);

        // Verify output count matches channel count
        if (num_outs != _num_channels) {
            std::cerr << "SourceUHDBlock: Output count mismatch - expected " << _num_channels
                      << " outputs but got " << num_outs << std::endl;
            return cler::Error::TERM_ProcedureError;
        }

        // Get DBF write pointers from all outputs
        size_t idx = 0;
        auto get_write_ptrs = [&](auto*... chs) {
            ((std::tie(_write_ptrs[idx], _write_sizes[idx]) = chs->write_dbf(), idx++), ...);
        };
        get_write_ptrs(outs...);

        // Find minimum space across all channels
        size_t min_space = _write_sizes[0];
        for (size_t i = 1; i < num_outs; ++i) {
            min_space = std::min(min_space, _write_sizes[i]);
        }

        if (min_space == 0) {
            return cler::Error::NotEnoughSpace;
        }

        // Limit to max samples per packet to avoid fragmentation
        size_t to_read = std::min(min_space, max_samps_per_packet);

        // Fill UHD buffer pointer array
        for (size_t i = 0; i < num_outs; ++i) {
            _uhd_buffs[i] = _write_ptrs[i];
        }

        // Receive samples directly into all output buffers (multi-channel atomic recv)
        uhd::rx_metadata_t md;
        size_t num_rx = rx_stream->recv(_uhd_buffs.data(), to_read, md, 0.1);  // 100ms timeout

        // Store metadata for user access (shared across all channels)
        last_rx_metadata.has_time_spec = md.has_time_spec;
        if (md.has_time_spec) {
            last_rx_metadata.time_seconds = md.time_spec.get_full_secs();
            last_rx_metadata.time_frac_seconds = md.time_spec.get_frac_secs();
        }
        last_rx_metadata.start_of_burst = md.start_of_burst;
        last_rx_metadata.end_of_burst = md.end_of_burst;
        last_rx_metadata.more_fragments = md.more_fragments;
        last_rx_metadata.error_code = md.error_code;

        // Handle metadata errors
        switch(md.error_code) {
            case uhd::rx_metadata_t::ERROR_CODE_NONE:
                // Success
                break;

            case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
                return cler::Error::NotEnoughSamples;

            case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
                overflow_count++;
                if (overflow_count % 100 == 0) {
                    std::cerr << "SourceUHDBlock: Overflow count: " << overflow_count << std::endl;
                }
                // Continue despite overflow
                break;

            case uhd::rx_metadata_t::ERROR_CODE_LATE_COMMAND:
                std::cerr << "SourceUHDBlock: Late command at "
                          << md.time_spec.get_real_secs() << "s" << std::endl;
                break;

            case uhd::rx_metadata_t::ERROR_CODE_BROKEN_CHAIN:
                std::cerr << "SourceUHDBlock: Broken chain - samples lost" << std::endl;
                break;

            case uhd::rx_metadata_t::ERROR_CODE_ALIGNMENT:
                std::cerr << "SourceUHDBlock: Multi-channel alignment error" << std::endl;
                return cler::Error::TERM_ProcedureError;

            case uhd::rx_metadata_t::ERROR_CODE_BAD_PACKET:
                std::cerr << "SourceUHDBlock: Bad packet received" << std::endl;
                break;

            default:
                std::cerr << "SourceUHDBlock: Unknown error code: "
                          << md.strerror() << std::endl;
                return cler::Error::TERM_ProcedureError;
        }

        if (num_rx > 0) {
            // Commit writes to all channels using fold expression
            idx = 0;
            ((outs->commit_write(num_rx), idx++), ...);

            return cler::Empty{};
        }

        return cler::Error::NotEnoughSamples;
    }

    // Get last RX metadata
    const RxMetadata& get_last_metadata() const { return last_rx_metadata; }

    // Control methods (per-channel)
    void set_frequency(double freq, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        uhd::tune_request_t tune_req(freq);
        usrp->set_rx_freq(tune_req, channel);
        if (channel == 0) {
            center_freq = freq;
        }
    }

    void set_gain(double gain, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_gain(gain, channel);
        if (channel == 0) {
            gain_db = gain;
        }
    }

    void set_sample_rate(double rate, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_rate(rate, channel);
        double actual_rate = usrp->get_rx_rate(channel);
        if (channel == 0) {
            sample_rate = actual_rate;
        }
    }

    void set_bandwidth(double bw, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_bandwidth(bw, channel);
    }

    void set_antenna(const std::string& antenna, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        auto antennas = usrp->get_rx_antennas(channel);
        if (std::find(antennas.begin(), antennas.end(), antenna) == antennas.end()) {
            std::stringstream ss;
            ss << "Antenna '" << antenna << "' not supported on channel " << channel
               << ". Available antennas: ";
            for (const auto& ant : antennas) {
                ss << ant << " ";
            }
            throw std::runtime_error(ss.str());
        }
        usrp->set_rx_antenna(antenna, channel);
    }

    void set_dc_offset_auto(bool enable, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_dc_offset(enable, channel);
    }

    void set_iq_balance_auto(bool enable, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_iq_balance(enable, channel);
    }

    void set_agc(bool enable, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_agc(enable, channel);
    }

    // Getters (channel 0 values cached for backward compatibility)
    double get_frequency(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return channel == 0 ? center_freq : usrp->get_rx_freq(channel);
    }

    double get_gain(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return channel == 0 ? gain_db : usrp->get_rx_gain(channel);
    }

    double get_sample_rate(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return channel == 0 ? sample_rate : usrp->get_rx_rate(channel);
    }

    double get_bandwidth(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_bandwidth(channel);
    }

    std::string get_antenna(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_antenna(channel);
    }

    std::vector<std::string> list_antennas(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_antennas(channel);
    }

    uhd::freq_range_t get_frequency_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_freq_range(channel);
    }

    uhd::gain_range_t get_gain_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_gain_range(channel);
    }

    std::vector<std::string> list_gains(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_gain_names(channel);
    }

    uhd::gain_range_t get_gain_range(const std::string& name, size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_gain_range(name, channel);
    }

    uhd::meta_range_t get_sample_rate_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_rates(channel);
    }

    uhd::freq_range_t get_bandwidth_range(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_bandwidth_range(channel);
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
    size_t get_overflow_count() const { return overflow_count; }
    void reset_overflow_count() { overflow_count = 0; }

    // ========== ADVANCED FEATURES ==========

    // Timed Commands
    void set_command_time(double time_seconds, double frac_seconds = 0.0) {
        usrp->set_command_time(uhd::time_spec_t(time_seconds, frac_seconds));
        command_time_set = true;
    }

    void clear_command_time() {
        usrp->clear_command_time();
        command_time_set = false;
    }

    // Timed operations (execute at command time if set, per-channel)
    void set_frequency_timed(double freq, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        uhd::tune_request_t tune_req(freq);
        usrp->set_rx_freq(tune_req, channel);
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
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_gain(gain, channel);
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
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_rx_antenna(antenna, channel);
        if (command_time_set) {
            usrp->clear_command_time();
            command_time_set = false;
        }
    }

    // GPIO Control (per-channel)
    void gpio_set_ctrl(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "CTRL", value, mask, channel);
    }

    void gpio_set_ddr(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "DDR", value, mask, channel);
    }

    void gpio_set_out(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        usrp->set_gpio_attr(bank, "OUT", value, mask, channel);
    }

    uint32_t gpio_get_in(const std::string& bank, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_gpio_attr(bank, "READBACK", channel);
    }

    // Timed GPIO (must call set_command_time first, per-channel)
    void gpio_set_out_timed(const std::string& bank, uint32_t value, uint32_t mask = 0xFFFFFFFF, size_t channel = 0) {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
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
        return usrp->get_clock_sources(0);  // Mboard 0
    }

    std::vector<std::string> get_time_sources() const {
        return usrp->get_time_sources(0);  // Mboard 0
    }

    // Sensor Access (per-channel)
    std::vector<std::string> get_rx_sensor_names(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_sensor_names(channel);
    }

    std::string get_rx_sensor(const std::string& name, size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        return usrp->get_rx_sensor(name, channel).to_pp_string();
    }

    std::vector<std::string> get_mboard_sensor_names() const {
        return usrp->get_mboard_sensor_names();
    }

    std::string get_mboard_sensor(const std::string& name) const {
        return usrp->get_mboard_sensor(name).to_pp_string();
    }

    bool is_lo_locked(size_t channel = 0) const {
        if (channel >= _num_channels) {
            throw std::out_of_range("SourceUHDBlock: Channel index out of range");
        }
        auto sensors = usrp->get_rx_sensor_names(channel);
        if (std::find(sensors.begin(), sensors.end(), "lo_locked") != sensors.end()) {
            return usrp->get_rx_sensor("lo_locked", channel).to_bool();
        }
        return true;  // Assume locked if sensor doesn't exist
    }

    bool is_ref_locked() const {
        auto sensors = usrp->get_mboard_sensor_names();
        if (std::find(sensors.begin(), sensors.end(), "ref_locked") != sensors.end()) {
            return usrp->get_mboard_sensor("ref_locked").to_bool();
        }
        return true;  // Assume locked if sensor doesn't exist
    }

    // Multi-USRP Synchronization Helper
    void sync_all_devices() {
        // Synchronize all devices in multi-USRP configuration
        // Assumes external 10 MHz + PPS already configured

        std::cout << "Synchronizing USRP devices..." << std::endl;

        // Wait for PPS edge
        auto last_pps = usrp->get_time_last_pps();
        while (last_pps == usrp->get_time_last_pps()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Set time on next PPS
        usrp->set_time_next_pps(uhd::time_spec_t(0.0));

        // Wait for next PPS to ensure sync
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::cout << "USRP devices synchronized at t=0" << std::endl;
    }

protected:
    // Allow derived classes to access USRP
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_stream;

private:
    // Configuration
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t _num_channels;
    std::string wire_format;  // OTW format (sc16, sc8, fc32, etc.)

    // Multi-channel support
    std::vector<void*> _uhd_buffs;  // Buffer pointers for UHD multi-channel recv()
    std::vector<T*> _write_ptrs;    // Temp storage for write_dbf pointers
    std::vector<size_t> _write_sizes;  // Temp storage for write_dbf sizes

    // Streaming
    size_t max_samps_per_packet;

    // Metadata
    RxMetadata last_rx_metadata;

    // Timed command tracking
    bool command_time_set = false;

    // Statistics
    size_t overflow_count = 0;
};

// Common template instantiations (COMPLEX TYPES ONLY)
// UHD operates on I/Q pairs - scalar types are not supported
using SourceUHDBlockCF32 = SourceUHDBlock<std::complex<float>>;
using SourceUHDBlockSC16 = SourceUHDBlock<std::complex<int16_t>>;
using SourceUHDBlockSC8 = SourceUHDBlock<std::complex<int8_t>>;

// Helper function for device enumeration
struct UHDDeviceInfo {
    std::string type;
    std::string serial;
    std::string name;
    std::string product;
    uhd::device_addr_t args;

    std::string get_args_string() const {
        return args.to_string();
    }
};

inline std::vector<UHDDeviceInfo> enumerate_usrp_devices() {
    std::vector<UHDDeviceInfo> devices;
    auto results = uhd::device::find(uhd::device_addr_t());

    for (const auto& result : results) {
        UHDDeviceInfo info;
        info.args = result;

        // Extract key fields
        if (result.has_key("type")) info.type = result["type"];
        if (result.has_key("serial")) info.serial = result["serial"];
        if (result.has_key("name")) info.name = result["name"];
        if (result.has_key("product")) info.product = result["product"];

        devices.push_back(info);
    }

    return devices;
}
