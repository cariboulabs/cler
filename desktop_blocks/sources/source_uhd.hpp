#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
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
#include <numeric>
#include <atomic>
#include <mutex>

template<typename T>
struct SourceUHDBlock : public cler::BlockBase {
    static constexpr bool may_block = true;

    SourceUHDBlock(const char* name,
                   double freq,
                   double rate,
                   const std::string& dvc_adrs = "",
                   double gain = 20.0,
                   size_t num_channels = 1,
                   const std::string& otw_format = "sc16",
                   bool quiet = false)
        : BlockBase(name),
          center_freq(freq),
          sample_rate(rate),
          device_address(dvc_adrs),
          gain_db(gain),
          _num_channels(num_channels),
          wire_format(otw_format),
          _quiet(quiet),
          _configuring(false) {

        usrp = uhd::usrp::multi_usrp::make(device_address);
        if (!usrp) {
            cler::panic("SourceUHDBlock: Failed to create USRP device");
        }
        if (num_channels > usrp->get_rx_num_channels()) {
            cler::panic("SourceUHDBlock: Not enough RX channels");
        }
        _write_ptrs.resize(num_channels);
        _write_sizes.resize(num_channels);
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
        // Hardware may snap the requested rate; sample_rate now holds the actual value.
        _actual_rate.store(sample_rate, std::memory_order_release);
        _last_applied_rate = sample_rate;
        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels.resize(num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

        rx_stream = usrp->get_rx_stream(stream_args);
        if (!rx_stream) {
            cler::panic("SourceUHDBlock: Failed to setup RX stream");
        }
        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);
        // quiet=true for apps that reconfigure immediately after construction,
        // since these values would only describe the ctor args, not the final state.
        if (!_quiet) {
            std::cout << "SourceUHDBlock: Initialized " << usrp->get_mboard_name() << std::endl;
            std::cout << "  Channels: " << num_channels << std::endl;
            std::cout << "  Frequency: " << center_freq/1e6 << " MHz" << std::endl;
            std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS" << std::endl;
            std::cout << "  Gain: " << gain_db << " dB" << std::endl;
            std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
        }
    }

    ~SourceUHDBlock() {
        if (overflow_count > 0) {
            std::cout << "SourceUHDBlock: Total overflows: " << overflow_count << std::endl;
        }
    }

    bool configure(const UHDConfig& config, size_t channel = 0) {
        _configuring = true;

        // try/catch kept despite the style guide: the UHD API throws on errors.
        try {
            // Skip set_rx_rate when unchanged: callers re-send the same rate on
            // every freq/gain update, and a mid-stream set_rx_rate can glitch
            // some devices (e.g. B2xx).
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

            auto freq_range = usrp->get_rx_freq_range(channel);
            if (config.center_freq_Hz < freq_range.start() || 
                config.center_freq_Hz > freq_range.stop()) {
                std::cerr << "Frequency " << config.center_freq_Hz/1e6 
                          << " MHz out of range" << std::endl;
            }
            usrp->set_rx_freq(uhd::tune_request_t(config.center_freq_Hz), channel);

            auto gain_range = usrp->get_rx_gain_range(channel);
            if (config.gain < gain_range.start() || config.gain > gain_range.stop()) {
                std::cerr << "Gain " << config.gain << " dB out of range" << std::endl;
            }
            usrp->set_rx_gain(config.gain, channel);

            // Same no-op skip as the rate above.
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

    // The driver may snap a requested rate to what its clocking supports;
    // this is the actual running rate, derived values must use it, not the
    // requested one. Written by the ctor and by configure() (streaming
    // thread); safe to read from any thread (e.g. a GUI polling for a change).
    double actual_sample_rate() const {
        return _actual_rate.load(std::memory_order_acquire);
    }

    // Callable from any thread; only stages the request here. It is applied by
    // the streaming thread at the top of procedure(), since multi_usrp is not
    // safe against a concurrent recv().
    void request_configure(const UHDConfig& cfg) {
        std::lock_guard<std::mutex> lk(_cfg_mutex);
        _pending_cfg = cfg;
        _pending_cfg_gen.fetch_add(1, std::memory_order_release);
    }

    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);
        if (num_outs != _num_channels) {
            cler::panic("SourceUHDBlock: number of procedure() output channels must match num_channels from constructor");
        }

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
            return cler::Error::NotEnoughSpace;
        }

        size_t probe_ch = 0;
        bool all_have_space = true;
        ([&](OChannels* out) {
            auto [wp, ws] = out->write_dbf();
            _write_ptrs[probe_ch] = wp;
            _write_sizes[probe_ch] = ws;
            if (!wp || ws == 0) all_have_space = false;
            ++probe_ch;
        }(outs), ...);
        if (!all_have_space) {
            return cler::Error::NotEnoughSpace;
        }

        uhd::rx_metadata_t md;
        cler::Error result_error = cler::Error::OK;
        size_t total_rx = 0;
        size_t recv_ch = 0;
        [&]<std::size_t... Is>(std::index_sequence<Is...>) {
            ([&](OChannels* out) {
                const size_t ch = recv_ch++;

                if (result_error != cler::Error::OK) {
                    return;
                }
                T* write_ptr = _write_ptrs[ch];
                const size_t write_size = _write_sizes[ch];
                size_t num_rx = 0;
                try {
                    num_rx = rx_stream->recv(write_ptr, write_size, md, 0.1);
                } catch (const std::exception& e) {
                    std::cerr << "SourceUHDBlock: recv failed: " << e.what() << std::endl;
                    result_error = cler::Error::TERM_ProcedureError;
                    return;
                }
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
                total_rx += num_rx;
            }(outs), ...);
        }(std::make_index_sequence<num_outs>{});



        if (result_error != cler::Error::OK) {
            return result_error;
        }
        if (total_rx == 0) {
            return cler::Error::NotEnoughSamples;
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
    std::vector<T*> _write_ptrs;
    std::vector<size_t> _write_sizes;
    std::string wire_format;
    bool _quiet;
    std::atomic<bool> _configuring;
    size_t overflow_count = 0;

    // Requested rates/bandwidths closer than this are treated as identical.
    static constexpr double RATE_EPSILON_HZ = 0.5;

    // Touched only by the ctor and configure() (streaming thread).
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