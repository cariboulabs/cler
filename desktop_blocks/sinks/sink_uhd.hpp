#pragma once

#include "cler.hpp"
#include "desktop_blocks/utils/usrp_common.hpp"

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
                 const std::string& dvc_adrs = "",
                 size_t num_channels = 1,
                 size_t channel_size = 0,
                 const std::string& otw_format = "sc16",
                 const USRPConfig* initial_config = nullptr)
        : BlockBase(name),
          _device_address(dvc_adrs),
          _num_channels(num_channels),
          _wire_format(otw_format),
          _configuring(false) {

        if (_num_channels == 0) {
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
            ::operator new[](_num_channels * sizeof(cler::Channel<T>))
        );

        // Construct channels with placement new (first try/catch for channel construction)
        size_t constructed_channels = 0;
        try {
            for (size_t i = 0; i < _num_channels; ++i) {
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
            _usrp = uhd::usrp::multi_usrp::make(_device_address);
            if (!_usrp) {
                throw std::runtime_error("SinkUHDBlock: Failed to create USRP device with args: " + _device_address);
            }

            // Set thread priority for better performance
            uhd::set_thread_priority_safe(0.5, true);

            // Setup TX stream for all channels
            // CPU format: what the host sees (fc32, sc16, sc8)
            // OTW format: what goes over the wire (sc16, sc8, fc32, etc.)
            uhd::stream_args_t stream_args(get_uhd_format<T>(), _wire_format);
            stream_args.channels.resize(_num_channels);
            std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

            _tx_stream = _usrp->get_tx_stream(stream_args);
            if (!_tx_stream) {
                throw std::runtime_error("SinkUHDBlock: Failed to setup TX stream");
            }

            for (size_t channel = 0; channel < _num_channels; ++channel) {
                configure(*initial_config, channel);
            }

            // Allocate UHD buffer pointer array (vector auto-manages)
            _uhd_buffs.resize(_num_channels);
            _read_ptrs.resize(_num_channels);
            _read_sizes.resize(_num_channels);



            USRPConfig config_to_use;
            if (initial_config) {
                config_to_use = *initial_config;
                std::cout << "  Using provided initial configuration" << std::endl;
            } else {
                std::cout << "  Using default configuration:" << std::endl;
            }
            // Configure all channels
            for (size_t ch = 0; ch < _num_channels; ++ch) {
                if (!configure(config_to_use, ch)) {
                    throw std::runtime_error("Failed to configure channel " + std::to_string(ch));
                }
            }
            // Print device info
            std::cout << "SinkUHDBlock: Initialized "
                      << _usrp->get_mboard_name() << " / " << _usrp->get_pp_string()
                      << std::endl;
            std::cout << "  Channels: " << _num_channels << std::endl;
            std::cout << "  Frequency: " << config_to_use.center_freq_Hz/1e6 << " MHz (all channels)" << std::endl;
            std::cout << "  Sample rate: " << config_to_use.sample_rate_Hz/1e6 << " MSPS (all channels)" << std::endl;
            std::cout << "  Gain: " << config_to_use.gain << " dB (all channels)" << std::endl;
            std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << _wire_format << std::endl;

        } catch (...) {
            // Cleanup channels on any exception during setup
            cleanup_channels(in, _num_channels);
            in = nullptr;
            throw;  // Re-throw the original exception
        }
    }

    ~SinkUHDBlock() {
        // Clean up input channel array
        if (in) {
            cleanup_channels(in, _num_channels);
        }
        // Print statistics
        if (underflow_count > 0) {
            std::cout << "SinkUHDBlock: Total underflows: " << underflow_count << std::endl;
        }
    }

    bool configure(const USRPConfig& config, size_t channel = 0) {
    _configuring = true;
        try {
            // Set sample rate
            _usrp->set_tx_rate(config.sample_rate_Hz, channel);
            double actual_rate = _usrp->get_tx_rate(channel);
            if (std::abs(actual_rate - config.sample_rate_Hz) > 1.0) {
                std::cout << "Warning: Requested " << config.sample_rate_Hz/1e6 
                          << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
            }

            // Set frequency
            auto freq_range = _usrp->get_tx_freq_range(channel);
            if (config.center_freq_Hz < freq_range.start() || 
                config.center_freq_Hz > freq_range.stop()) {
                std::cerr << "Frequency " << config.center_freq_Hz/1e6 
                          << " MHz out of range" << std::endl;
            }
            _usrp->set_tx_freq(uhd::tune_request_t(config.center_freq_Hz), channel);

            // Set gain
            auto gain_range = _usrp->get_tx_gain_range(channel);
            if (config.gain < gain_range.start() || config.gain > gain_range.stop()) {
                std::cerr << "Gain " << config.gain << " dB out of range" << std::endl;
            }
            _usrp->set_tx_gain(config.gain, channel);

            // Set bandwidth (if specified)
            if (config.bandwidth_Hz > 0) {
                _usrp->set_tx_bandwidth(config.bandwidth_Hz, channel);
            }
            _configuring = false;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Configuration failed: " << e.what() << std::endl;
            _configuring = false;
            return false;
        }
    }

    cler::Result<cler::Empty, cler::Error> procedure() {
        if (_configuring.load(std::memory_order_acquire)) {
            return cler::Empty{};  // Skip this iteration
        }
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

            size_t sent = _tx_stream->send(_read_ptrs[i],
                            _read_sizes[i],
                            md,
                            0.1);  // 100ms timeout
            in->commit_read(sent);
        }

        // Check for async events
        handle_async_events();

        return cler::Empty{};
    }

    // Poll for async TX events (non-blocking)
    bool poll_async_event(AsyncTxEvent& event, double timeout = 0.0) {
        uhd::async_metadata_t async_md;
        if (_tx_stream->recv_async_msg(async_md, timeout)) {
            event.event_occurred = true;
            event.event_code = async_md.event_code;
            event.time_seconds = async_md.time_spec.get_full_secs();
            event.time_frac_seconds = async_md.time_spec.get_frac_secs();
            return true;
        }
        event.event_occurred = false;
        return false;
    }

    // Statistics
    size_t get_underflow_count() const { return underflow_count; }
    void reset_underflow_count() { underflow_count = 0; }

    // ========== ADVANCED FEATURES ==========
    void sync_all_devices() {
        std::cout << "Synchronizing USRP devices..." << std::endl;
        auto last_pps = _usrp->get_time_last_pps();
        while (last_pps == _usrp->get_time_last_pps()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        _usrp->set_time_next_pps(uhd::time_spec_t(0.0));
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "USRP devices synchronized at t=0" << std::endl;
    }

protected:
    // Allow derived classes to access USRP
    uhd::usrp::multi_usrp::sptr _usrp;
    uhd::tx_streamer::sptr _tx_stream;

private:
    void handle_async_events() {
        uhd::async_metadata_t async_md;
        while (_tx_stream->recv_async_msg(async_md, 0.0)) {  // Non-blocking
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

    USRPConfig _current_config; 
    std::string _device_address;
    size_t _num_channels;
    std::string _wire_format;
    std::atomic<bool> _configuring;

    // Multi-channel support
    std::vector<void*> _uhd_buffs;  // Buffer pointers for UHD multi-channel send()
    std::vector<const T*> _read_ptrs;  // Temp storage for read_dbf pointers
    std::vector<size_t> _read_sizes;   // Temp storage for read_dbf sizes

    // Statistics
    size_t underflow_count = 0;
};

// UHD operates on I/Q pairs - scalar types are not supported
using SinkUHDBlockCF32 = SinkUHDBlock<std::complex<float>>;
using SinkUHDBlockSC16 = SinkUHDBlock<std::complex<int16_t>>;
using SinkUHDBlockSC8 = SinkUHDBlock<std::complex<int8_t>>;
