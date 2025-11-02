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
#include <numeric>

template<typename T>
inline std::string get_uhd_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return "fc32";
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return "sc16";
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return "sc8";
    } else {
        // Only trigger static_assert for unsupported types
        static_assert(std::is_same_v<T, void>, 
            "UHD blocks only support complex<float>, complex<int16_t>, complex<int8_t>");
    }
}

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
          wire_format(otw_format)
    {
        if (_num_channels == 0) throw std::invalid_argument("num_channels must be >= 1");

        // Create USRP
        _usrp = uhd::usrp::multi_usrp::make(device_args);
        if (!_usrp) throw std::runtime_error("Failed to create USRP");

        // Set thread priority
        uhd::set_thread_priority_safe(0.5, true);

        // Configure each channel
        for (size_t ch = 0; ch < _num_channels; ++ch) {
            _usrp->set_rx_rate(sample_rate, ch);
            _usrp->set_rx_freq(uhd::tune_request_t(center_freq), ch);
            _usrp->set_rx_gain(gain_db, ch);
        }

        // Setup RX stream
        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels.resize(_num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);
        rx_stream = _usrp->get_rx_stream(stream_args);

        if (!rx_stream) throw std::runtime_error("Failed to create RX stream");

        // Issue continuous streaming command
        uhd::stream_cmd_t cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        cmd.stream_now = true;
        rx_stream->issue_stream_cmd(cmd);

        std::cout << "Zohar - SourceUHDBlock initialized: " 
                  << _num_channels << " channel(s), freq=" 
                  << center_freq << " Hz, rate=" << sample_rate << " S/s\n";
    }

    ~SourceUHDBlock() {
        if (rx_stream) {
            uhd::stream_cmd_t stop_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            stop_cmd.stream_now = true;
            try { rx_stream->issue_stream_cmd(stop_cmd); } catch(...) {}
        }
    }
    
cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
    uhd::rx_metadata_t md;

    auto [write_ptr, write_size] = out->write_dbf();
    if (!write_ptr || write_size == 0) {
        return cler::Error::NotEnoughSpace;
    }
    size_t num_rx = rx_stream->recv(write_ptr, write_size, md); // need to be minimum of write_zise and max_samps_per_packet?? 
    if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
        return cler::Error::NotEnoughSamples;
    }
    if (num_rx == 0) {
        return cler::Empty{};  // Don't error out, just try again
    }
    size_t processed = std::min(num_rx, write_size);
    out->commit_write(processed);
    return cler::Empty{};
}


private:
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t _num_channels;
    std::string wire_format;

    uhd::usrp::multi_usrp::sptr _usrp;
    uhd::rx_streamer::sptr rx_stream;
    size_t max_samps_per_packet = 0;
};
