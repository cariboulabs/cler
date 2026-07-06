#pragma once

#include "cler.hpp"
#include "desktop_blocks/misc/uhd_common.hpp"

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

#include <vector>
#include <string>
#include <iostream>
#include <numeric>
#include <atomic>
#include <mutex>

template<typename T>
struct SourceUHDBlock : public cler::BlockBase {

    SourceUHDBlock(const char* name,
                   double freq,
                   double rate,
                   const std::string& dvc_adrs = "",
                   double gain = 20.0,
                   size_t num_channels = 1,
                   const std::string& otw_format = "sc16")
        : BlockBase(name),
          center_freq(freq),
          sample_rate(rate),
          device_address(dvc_adrs),
          gain_db(gain),
          _num_channels(num_channels),
          wire_format(otw_format),
          _configuring(false) {

        usrp = uhd::usrp::multi_usrp::make(device_address);
        if (!usrp) {
            throw std::runtime_error("SourceUHDBlock: Failed to create USRP device");
        }
        if (num_channels > usrp->get_rx_num_channels()) {
            throw std::runtime_error("SourceUHDBlock: Not enough RX channels");
        }
        uhd::set_thread_priority_safe(0.5, true);
        for (size_t ch = 0; ch < num_channels; ++ch) {
            usrp->set_rx_rate(sample_rate, ch);
            double actual_rate = usrp->get_rx_rate(ch);
            if (ch == 0 && std::abs(actual_rate - sample_rate) > 1.0) {
                sample_rate = actual_rate;
            }
            usrp->set_rx_freq(uhd::tune_request_t(center_freq), ch);
            usrp->set_rx_gain(gain_db, ch);
        }
        // The hardware may have snapped the requested rate (sample_rate now
        // holds the actual value); expose it and remember it as the last
        // applied rate so configure() can skip redundant set_rx_rate calls.
        _actual_rate.store(sample_rate, std::memory_order_release);
        _last_applied_rate = sample_rate;
        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels.resize(num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

        rx_stream = usrp->get_rx_stream(stream_args);
        if (!rx_stream) {
            throw std::runtime_error("SourceUHDBlock: Failed to setup RX stream");
        }
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);
        std::cout << "SourceUHDBlock: Initialized " << usrp->get_mboard_name() << std::endl;
        std::cout << "  Channels: " << num_channels << std::endl;
        std::cout << "  Frequency: " << center_freq/1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS" << std::endl;
        std::cout << "  Gain: " << gain_db << " dB" << std::endl;
        std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
    }

    ~SourceUHDBlock() {
        if (overflow_count > 0) {
            std::cout << "SourceUHDBlock: Total overflows: " << overflow_count << std::endl;
        }
    }

    bool configure(const UHDConfig& config, size_t channel = 0) {
        _configuring = true;
        
        //We want to avoid try-catch but UHD API crashes on errors
        try {
            // Set sample rate -- skipped when the requested rate equals the
            // last-applied request: callers that reconfigure freq/gain re-send
            // an unchanged rate every time, and set_rx_rate mid-stream can
            // glitch some devices (e.g. B2xx). Skipping a true no-op is
            // behavior-preserving for callers that do change the rate.
            if (std::abs(config.sample_rate_Hz - _last_applied_rate) > RATE_EPSILON_HZ) {
                usrp->set_rx_rate(config.sample_rate_Hz, channel);
                double actual_rate = usrp->get_rx_rate(channel);
                if (std::abs(actual_rate - config.sample_rate_Hz) > 1.0) {
                    std::cout << "Warning: Requested " << config.sample_rate_Hz/1e6
                              << " MSPS, got " << actual_rate/1e6 << " MSPS" << std::endl;
                }
                _last_applied_rate = config.sample_rate_Hz;
                _actual_rate.store(actual_rate, std::memory_order_release);
            }

            // Set frequency
            auto freq_range = usrp->get_rx_freq_range(channel);
            if (config.center_freq_Hz < freq_range.start() || 
                config.center_freq_Hz > freq_range.stop()) {
                std::cerr << "Frequency " << config.center_freq_Hz/1e6 
                          << " MHz out of range" << std::endl;
            }
            usrp->set_rx_freq(uhd::tune_request_t(config.center_freq_Hz), channel);

            // Set gain
            auto gain_range = usrp->get_rx_gain_range(channel);
            if (config.gain < gain_range.start() || config.gain > gain_range.stop()) {
                std::cerr << "Gain " << config.gain << " dB out of range" << std::endl;
            }
            usrp->set_rx_gain(config.gain, channel);

            // Set bandwidth (if specified) -- skipped when unchanged for the
            // same reason as the rate above (callers tie it to the rate).
            if (config.bandwidth_Hz > 0 &&
                std::abs(config.bandwidth_Hz - _last_applied_bw) > RATE_EPSILON_HZ) {
                usrp->set_rx_bandwidth(config.bandwidth_Hz, channel);
                _last_applied_bw = config.bandwidth_Hz;
            }
            _configuring = false;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "Configuration failed: " << e.what() << std::endl;
            _configuring = false;
            return false;
        }
    }

    // Rate the hardware is ACTUALLY running at: the driver may snap a
    // requested rate to what its clocking supports. Written at construction
    // and by configure() (streaming thread) after a rate change; readable
    // from any thread (e.g. a GUI polling for the change to land).
    double actual_sample_rate() const {
        return _actual_rate.load(std::memory_order_acquire);
    }

    // Thread-safe live reconfigure: callable from any thread (e.g. the GUI).
    // The request is only STAGED here; it is actually applied by the streaming
    // thread itself at the top of procedure(), so we never touch the USRP from
    // two threads at once (UHD multi_usrp is not safe against a concurrent recv).
    void request_configure(const UHDConfig& cfg) {
        std::lock_guard<std::mutex> lk(_cfg_mutex);
        _pending_cfg = cfg;
        _pending_cfg_gen.fetch_add(1, std::memory_order_release);
    }

    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        assert(num_outs == _num_channels && "Number of output channels defined in block constructor must match the number of channels");

        // Apply any staged reconfigure in this (the streaming) thread.
        if (_pending_cfg_gen.load(std::memory_order_acquire) != _applied_cfg_gen) {
            UHDConfig cfg;
            {
                std::lock_guard<std::mutex> lk(_cfg_mutex);
                cfg = _pending_cfg;
                _applied_cfg_gen = _pending_cfg_gen.load(std::memory_order_acquire);
            }
            configure(cfg, 0);
        }

        if (_configuring.load(std::memory_order_acquire)) {
            return cler::Empty{};  // Skip this iteration
        }

        uhd::rx_metadata_t md;
        cler::Error result_error = cler::Error::OK;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&](OChannels* out) {

                if (result_error != cler::Error::OK) {
                    return;
                }
                auto [write_ptr, write_size] = out->write_dbf();
                if (!write_ptr || write_size == 0) {
                    result_error = cler::Error::NotEnoughSpace;
                    return;
                }
                size_t num_rx = rx_stream->recv(write_ptr, write_size, md, 0.1);
                if (num_rx == 0) {
                    return;
                }
                if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                    overflow_count++;
                } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE &&
                            md.error_code != uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                    std::cerr << "SourceUHDBlock: " << md.strerror() << std::endl;
                    result_error = cler::Error::TERM_ProcedureError;
                    return;
                }

                out->commit_write(num_rx);
            }(outs), ...);
        }(std::make_index_sequence<num_outs>{});



        if (result_error != cler::Error::OK) {
            return result_error;
        }
        return cler::Empty{};


    }
    size_t get_overflow_count() const { return overflow_count; }

protected:
    uhd::usrp::multi_usrp::sptr usrp;
    uhd::rx_streamer::sptr rx_stream;

private:
    double center_freq;
    double sample_rate;
    std::string device_address;
    double gain_db;
    size_t _num_channels;
    std::string wire_format;
    std::atomic<bool> _configuring;
    size_t overflow_count = 0;

    // Requested rates/bandwidths closer than this are treated as identical.
    static constexpr double RATE_EPSILON_HZ = 0.5;

    // Actual device rate (see actual_sample_rate()). The last-applied REQUESTED
    // values let configure() skip redundant set_rx_rate/set_rx_bandwidth calls;
    // they are touched only by the ctor and configure() (streaming thread).
    std::atomic<double> _actual_rate{0.0};
    double _last_applied_rate = 0.0;
    double _last_applied_bw   = -1.0;   // <0: never set (ctor doesn't set it)

    // Live-reconfigure staging (written by any thread, applied by streaming thread)
    std::mutex _cfg_mutex;
    UHDConfig _pending_cfg;
    std::atomic<size_t> _pending_cfg_gen{0};
    size_t _applied_cfg_gen{0};
};

using SourceUHDBlockCF32 = SourceUHDBlock<std::complex<float>>;
using SourceUHDBlockSC16 = SourceUHDBlock<std::complex<int16_t>>;
using SourceUHDBlockSC8 = SourceUHDBlock<std::complex<int8_t>>;