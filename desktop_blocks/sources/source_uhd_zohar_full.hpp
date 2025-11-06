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

template<typename T>
inline std::string get_uhd_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return "fc32";
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return "sc16";
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return "sc8";
    } else {
        static_assert(!std::is_same_v<T, T>, "UHD blocks only support complex types");
    }
}

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
          wire_format(otw_format) {

        if (num_channels == 0) {
            throw std::invalid_argument("SourceUHDBlock: num_channels must be at least 1");
        }

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

        uhd::stream_args_t stream_args(get_uhd_format<T>(), wire_format);
        stream_args.channels.resize(num_channels);
        std::iota(stream_args.channels.begin(), stream_args.channels.end(), 0);

        rx_stream = usrp->get_rx_stream(stream_args);
        if (!rx_stream) {
            throw std::runtime_error("SourceUHDBlock: Failed to setup RX stream");
        }

        max_samps_per_packet = rx_stream->get_max_num_samps();

        uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
        stream_cmd.stream_now = true;
        rx_stream->issue_stream_cmd(stream_cmd);

        std::cout << "SourceUHDBlock: Initialized " << usrp->get_mboard_name() << std::endl;
        std::cout << "  Channels: " << num_channels << std::endl;
        std::cout << "  Frequency: " << center_freq/1e6 << " MHz" << std::endl;
        std::cout << "  Sample rate: " << sample_rate/1e6 << " MSPS" << std::endl;
        std::cout << "  Gain: " << gain_db << " dB" << std::endl;
        std::cout << "  Format: CPU=" << get_uhd_format<T>() << ", OTW=" << wire_format << std::endl;
        std::cout << "  Max samples/packet: " << max_samps_per_packet << std::endl;
        
        auto antennas = usrp->get_rx_antennas(0);
        if (!antennas.empty()) {
            std::cout << "  Available RX antennas: ";
            for (const auto& ant : antennas) std::cout << ant << " ";
            std::cout << "(using: " << usrp->get_rx_antenna(0) << ")" << std::endl;
        }
    }

    ~SourceUHDBlock() {
        if (rx_stream) {
            uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
            stream_cmd.stream_now = true;
            try { rx_stream->issue_stream_cmd(stream_cmd); } catch (...) {}
        }
        if (overflow_count > 0) {
            std::cout << "SourceUHDBlock: Total overflows: " << overflow_count << std::endl;
        }
    }

    template<typename... OChannels>
    cler::Result<cler::Empty, cler::Error> procedure(OChannels*... outs) {
        constexpr size_t num_outs = sizeof...(OChannels);

        if (num_outs != _num_channels) {
            std::cerr << "SourceUHDBlock: Channel count mismatch" << std::endl;
            return cler::Error::TERM_ProcedureError;
        }

        uhd::rx_metadata_t md;

        // Single channel - mimic working source_uhd_zohar.hpp exactly
        if constexpr (num_outs == 1) {
            auto out = std::get<0>(std::forward_as_tuple(outs...));
            auto [write_ptr, write_size] = out->write_dbf();
            
            if (!write_ptr || write_size == 0) {
                return cler::Error::NotEnoughSpace;
            }

            // Do exactly what zohar version does - no limiting
            size_t num_rx = rx_stream->recv(write_ptr, write_size, md, 0.1);

            last_rx_metadata.has_time_spec = md.has_time_spec;
            if (md.has_time_spec) {
                last_rx_metadata.time_seconds = md.time_spec.get_full_secs();
                last_rx_metadata.time_frac_seconds = md.time_spec.get_frac_secs();
            }
            last_rx_metadata.error_code = md.error_code;

            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                overflow_count++;
            } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE && 
                       md.error_code != uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                std::cerr << "SourceUHDBlock: " << md.strerror() << std::endl;
                return cler::Error::TERM_ProcedureError;
            }

            if (num_rx == 0) {
                return cler::Empty{};  // Like zohar - don't error, just return empty
            }

            out->commit_write(num_rx);
            return cler::Empty{};
        }
        // Multi-channel path
        else {
            void* buffs[num_outs];
            size_t sizes[num_outs];
            
            size_t idx = 0;
            auto get_ptrs = [&](auto*... chs) {
                ((std::tie(buffs[idx], sizes[idx]) = chs->write_dbf(), idx++), ...);
            };
            get_ptrs(outs...);

            size_t min_size = sizes[0];
            for (size_t i = 1; i < num_outs; ++i) {
                min_size = std::min(min_size, sizes[i]);
            }

            if (min_size == 0) {
                return cler::Error::NotEnoughSpace;
            }

            size_t num_rx = rx_stream->recv(buffs, min_size, md, 0.1);

            last_rx_metadata.has_time_spec = md.has_time_spec;
            if (md.has_time_spec) {
                last_rx_metadata.time_seconds = md.time_spec.get_full_secs();
                last_rx_metadata.time_frac_seconds = md.time_spec.get_frac_secs();
            }
            last_rx_metadata.error_code = md.error_code;

            if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
                overflow_count++;
            } else if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE && 
                       md.error_code != uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
                std::cerr << "SourceUHDBlock: " << md.strerror() << std::endl;
                return cler::Error::TERM_ProcedureError;
            }

            if (num_rx == 0) {
                return cler::Empty{};
            }

            idx = 0;
            ((outs->commit_write(num_rx), idx++), ...);
            return cler::Empty{};
        }
    }

    const RxMetadata& get_last_metadata() const { return last_rx_metadata; }
    size_t get_num_channels() const { return _num_channels; }
    size_t get_overflow_count() const { return overflow_count; }
    void reset_overflow_count() { overflow_count = 0; }

    double get_frequency(size_t ch = 0) const { return ch == 0 ? center_freq : usrp->get_rx_freq(ch); }
    double get_gain(size_t ch = 0) const { return ch == 0 ? gain_db : usrp->get_rx_gain(ch); }
    double get_sample_rate(size_t ch = 0) const { return ch == 0 ? sample_rate : usrp->get_rx_rate(ch); }
    double get_bandwidth(size_t ch = 0) const { return usrp->get_rx_bandwidth(ch); }
    std::string get_antenna(size_t ch = 0) const { return usrp->get_rx_antenna(ch); }
    std::vector<std::string> list_antennas(size_t ch = 0) const { return usrp->get_rx_antennas(ch); }
    uhd::freq_range_t get_frequency_range(size_t ch = 0) const { return usrp->get_rx_freq_range(ch); }
    uhd::gain_range_t get_gain_range(size_t ch = 0) const { return usrp->get_rx_gain_range(ch); }

    std::string get_mboard_name() const { return usrp->get_mboard_name(); }
    std::string get_pp_string() const { return usrp->get_pp_string(); }

    void set_frequency(double freq, size_t ch = 0) {
        usrp->set_rx_freq(uhd::tune_request_t(freq), ch);
        if (ch == 0) center_freq = freq;
    }

    void set_gain(double gain, size_t ch = 0) {
        usrp->set_rx_gain(gain, ch);
        if (ch == 0) gain_db = gain;
    }

    void set_sample_rate(double rate, size_t ch = 0) {
        usrp->set_rx_rate(rate, ch);
        if (ch == 0) sample_rate = usrp->get_rx_rate(ch);
    }

    void set_bandwidth(double bw, size_t ch = 0) { usrp->set_rx_bandwidth(bw, ch); }
    void set_antenna(const std::string& antenna, size_t ch = 0) { usrp->set_rx_antenna(antenna, ch); }
    void set_dc_offset_auto(bool enable, size_t ch = 0) { usrp->set_rx_dc_offset(enable, ch); }
    void set_iq_balance_auto(bool enable, size_t ch = 0) { usrp->set_rx_iq_balance(enable, ch); }
    void set_agc(bool enable, size_t ch = 0) { usrp->set_rx_agc(enable, ch); }

    // Advanced features
    void set_command_time(double time_sec, double frac_sec = 0.0) {
        usrp->set_command_time(uhd::time_spec_t(time_sec, frac_sec));
        command_time_set = true;
    }

    void clear_command_time() {
        usrp->clear_command_time();
        command_time_set = false;
    }

    void set_frequency_timed(double freq, size_t ch = 0) {
        usrp->set_rx_freq(uhd::tune_request_t(freq), ch);
        if (command_time_set) { usrp->clear_command_time(); command_time_set = false; }
        if (ch == 0) center_freq = freq;
    }

    void set_gain_timed(double gain, size_t ch = 0) {
        usrp->set_rx_gain(gain, ch);
        if (command_time_set) { usrp->clear_command_time(); command_time_set = false; }
        if (ch == 0) gain_db = gain;
    }

    void set_antenna_timed(const std::string& antenna, size_t ch = 0) {
        usrp->set_rx_antenna(antenna, ch);
        if (command_time_set) { usrp->clear_command_time(); command_time_set = false; }
    }

    // GPIO
    void gpio_set_ctrl(const std::string& bank, uint32_t val, uint32_t mask = 0xFFFFFFFF, size_t ch = 0) {
        usrp->set_gpio_attr(bank, "CTRL", val, mask, ch);
    }
    void gpio_set_ddr(const std::string& bank, uint32_t val, uint32_t mask = 0xFFFFFFFF, size_t ch = 0) {
        usrp->set_gpio_attr(bank, "DDR", val, mask, ch);
    }
    void gpio_set_out(const std::string& bank, uint32_t val, uint32_t mask = 0xFFFFFFFF, size_t ch = 0) {
        usrp->set_gpio_attr(bank, "OUT", val, mask, ch);
    }
    uint32_t gpio_get_in(const std::string& bank, size_t ch = 0) {
        return usrp->get_gpio_attr(bank, "READBACK", ch);
    }
    void gpio_set_out_timed(const std::string& bank, uint32_t val, uint32_t mask = 0xFFFFFFFF, size_t ch = 0) {
        usrp->set_gpio_attr(bank, "OUT", val, mask, ch);
        if (command_time_set) { usrp->clear_command_time(); command_time_set = false; }
    }

    // Time/Clock
    void set_clock_source(const std::string& src) { usrp->set_clock_source(src); }
    void set_time_source(const std::string& src) { usrp->set_time_source(src); }
    void set_time_now(double sec, double frac = 0.0) { usrp->set_time_now(uhd::time_spec_t(sec, frac)); }
    void set_time_next_pps(double sec, double frac = 0.0) { usrp->set_time_next_pps(uhd::time_spec_t(sec, frac)); }
    void set_time_unknown_pps(double sec, double frac = 0.0) { usrp->set_time_unknown_pps(uhd::time_spec_t(sec, frac)); }
    double get_time_now() const { return usrp->get_time_now().get_real_secs(); }
    double get_time_last_pps() const { return usrp->get_time_last_pps().get_real_secs(); }
    std::vector<std::string> get_clock_sources() const { return usrp->get_clock_sources(0); }
    std::vector<std::string> get_time_sources() const { return usrp->get_time_sources(0); }

    // Sensors
    std::vector<std::string> get_rx_sensor_names(size_t ch = 0) const { return usrp->get_rx_sensor_names(ch); }
    std::string get_rx_sensor(const std::string& name, size_t ch = 0) const {
        return usrp->get_rx_sensor(name, ch).to_pp_string();
    }
    std::vector<std::string> get_mboard_sensor_names() const { return usrp->get_mboard_sensor_names(); }
    std::string get_mboard_sensor(const std::string& name) const {
        return usrp->get_mboard_sensor(name).to_pp_string();
    }

    bool is_lo_locked(size_t ch = 0) const {
        auto sensors = usrp->get_rx_sensor_names(ch);
        if (std::find(sensors.begin(), sensors.end(), "lo_locked") != sensors.end()) {
            return usrp->get_rx_sensor("lo_locked", ch).to_bool();
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
        auto last_pps = usrp->get_time_last_pps();
        while (last_pps == usrp->get_time_last_pps()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        usrp->set_time_next_pps(uhd::time_spec_t(0.0));
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

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
    size_t max_samps_per_packet = 0;
    RxMetadata last_rx_metadata;
    bool command_time_set = false;
    size_t overflow_count = 0;
};

using SourceUHDBlockCF32 = SourceUHDBlock<std::complex<float>>;
using SourceUHDBlockSC16 = SourceUHDBlock<std::complex<int16_t>>;
using SourceUHDBlockSC8 = SourceUHDBlock<std::complex<int8_t>>;

struct UHDDeviceInfo {
    std::string type, serial, name, product;
    uhd::device_addr_t args;
    std::string get_args_string() const { return args.to_string(); }
};

inline std::vector<UHDDeviceInfo> enumerate_usrp_devices() {
    std::vector<UHDDeviceInfo> devices;
    auto results = uhd::device::find(uhd::device_addr_t());
    for (const auto& result : results) {
        UHDDeviceInfo info;
        info.args = result;
        if (result.has_key("type")) info.type = result["type"];
        if (result.has_key("serial")) info.serial = result["serial"];
        if (result.has_key("name")) info.name = result["name"];
        if (result.has_key("product")) info.product = result["product"];
        devices.push_back(info);
    }
    return devices;
}