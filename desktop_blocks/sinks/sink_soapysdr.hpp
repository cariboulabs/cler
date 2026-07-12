#pragma once

#include "cler.hpp"
#include "cler_desktop_utils.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include <complex>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

// Helper to map C++ types to SoapySDR format strings
template<typename T>
inline std::string get_soapy_format() {
    if constexpr (std::is_same_v<T, std::complex<float>>) {
        return SOAPY_SDR_CF32;
    } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
        return SOAPY_SDR_CS16;
    } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
        return SOAPY_SDR_CS8;
    } else if constexpr (std::is_same_v<T, std::complex<uint8_t>>) {
        return SOAPY_SDR_CU8;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return SOAPY_SDR_S32;
    } else if constexpr (std::is_same_v<T, int16_t>) {
        return SOAPY_SDR_S16;
    } else if constexpr (std::is_same_v<T, uint8_t>) {
        return SOAPY_SDR_U8;
    } else if constexpr (std::is_same_v<T, float>) {
        return SOAPY_SDR_F32;
    } else {
        static_assert(!std::is_same_v<T, T>, "Unsupported type for SoapySDR");
    }
}

template<typename T>
struct SinkSoapySDRBlock : public cler::BlockBase {
    
    cler::Channel<T> in;
    
    SinkSoapySDRBlock(const char* name,
                      const std::string& args,
                      double freq,
                      double rate,
                      double gain = 0.0,
                      size_t channel = 0,
                      size_t channel_size = 0)
        : BlockBase(name),
          in(channel_size == 0 ? cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(T) : channel_size),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          channel_idx(channel),
          device(nullptr),
          stream(nullptr) {
        
        if (channel_size > 0 && channel_size * sizeof(T) < cler::DOUBLY_MAPPED_MIN_SIZE) {
            cler::panic("Channel size too small for doubly-mapped buffers");
        }

        device = SoapySDR::Device::make(device_args);
        if (!device) {
            cler::panic(("SinkSoapySDRBlock: Failed to create SoapySDR device with args: " + device_args).c_str());
        }

        auto sample_rates = device->getSampleRateRange(SOAPY_SDR_TX, channel_idx);
        bool rate_valid = false;
        for (const auto& range : sample_rates) {
            if (sample_rate >= range.minimum() && sample_rate <= range.maximum()) {
                rate_valid = true;
                break;
            }
        }
        if (!rate_valid) {
            std::stringstream ss;
            ss << "Sample rate " << sample_rate/1e6 << " MSPS not supported. Supported rates: ";
            for (const auto& range : sample_rates) {
                ss << range.minimum()/1e6 << "-" << range.maximum()/1e6 << " MSPS ";
            }
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        device->setSampleRate(SOAPY_SDR_TX, channel_idx, sample_rate);

        auto freq_ranges = device->getFrequencyRange(SOAPY_SDR_TX, channel_idx);
        bool freq_valid = false;
        for (const auto& range : freq_ranges) {
            if (center_freq >= range.minimum() && center_freq <= range.maximum()) {
                freq_valid = true;
                break;
            }
        }
        if (!freq_valid) {
            std::stringstream ss;
            ss << "Frequency " << center_freq/1e6 << " MHz not supported. Supported ranges: ";
            for (const auto& range : freq_ranges) {
                ss << range.minimum()/1e6 << "-" << range.maximum()/1e6 << " MHz ";
            }
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        device->setFrequency(SOAPY_SDR_TX, channel_idx, center_freq);

        auto gain_range = device->getGainRange(SOAPY_SDR_TX, channel_idx);
        if (gain_db < gain_range.minimum() || gain_db > gain_range.maximum()) {
            std::stringstream ss;
            ss << "Gain " << gain_db << " dB not supported. Supported range: "
               << gain_range.minimum() << "-" << gain_range.maximum() << " dB";
            SoapySDR::Device::unmake(device);
            cler::panic(ss.str().c_str());
        }
        if (device->hasGainMode(SOAPY_SDR_TX, channel_idx)) {
            device->setGainMode(SOAPY_SDR_TX, channel_idx, false);
        }
        device->setGain(SOAPY_SDR_TX, channel_idx, gain_db);

        if (device->getBandwidthRange(SOAPY_SDR_TX, channel_idx).size() > 0) {
            device->setBandwidth(SOAPY_SDR_TX, channel_idx, sample_rate);
        }

        std::vector<size_t> channels = {channel_idx};
        std::string format = get_soapy_format<T>();

        stream = device->setupStream(SOAPY_SDR_TX, format, channels);
        if (!stream) {
            SoapySDR::Device::unmake(device);
            cler::panic("SinkSoapySDRBlock: Failed to setup TX stream");
        }

        mtu = device->getStreamMTU(stream);
        buffer.resize(mtu);

        int ret = device->activateStream(stream);
        if (ret != 0) {
            device->closeStream(stream);
            SoapySDR::Device::unmake(device);
            cler::panic(("SinkSoapySDRBlock: Failed to activate stream: " + std::string(SoapySDR::errToStr(ret))).c_str());
        }

        std::cout << "SinkSoapySDRBlock: Initialized " << device->getDriverKey()
                  << " (" << device->getHardwareKey() << ")"
                  << " at " << center_freq/1e6 << " MHz"
                  << ", " << sample_rate/1e6 << " MSPS"
                  << ", " << gain_db << " dB gain"
                  << ", MTU: " << mtu << " samples" << std::endl;

        auto antennas = device->listAntennas(SOAPY_SDR_TX, channel_idx);
        if (!antennas.empty()) {
            std::cout << "  Available TX antennas: ";
            for (const auto& ant : antennas) {
                std::cout << ant << " ";
            }
            std::cout << std::endl;
        }
    }
    
    ~SinkSoapySDRBlock() {
        if (stream && device) {
            device->deactivateStream(stream);
            device->closeStream(stream);
        }
        if (device) {
            SoapySDR::Device::unmake(device);
        }
    }
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        auto [read_ptr, read_size] = in.read_dbf();
        if (read_ptr == nullptr || read_size == 0) {
            return cler::Error::NotEnoughSamples;
        }

        size_t samples_sent = 0;
        while (samples_sent < read_size) {
            size_t to_send = std::min(mtu, read_size - samples_sent);

            // SoapySDR writeStream needs a void* array, so a copy is unavoidable here
            std::memcpy(buffer.data(), read_ptr + samples_sent, to_send * sizeof(T));

            void* buffs[] = {buffer.data()};
            int flags = 0;
            const long long time_ns = 0;
            const long timeout_us = 100000; // 100ms

            int ret = device->writeStream(stream, buffs, to_send, flags, time_ns, timeout_us);

            if (ret == SOAPY_SDR_TIMEOUT) {
                in.commit_read(samples_sent);
                return cler::Error::NotEnoughSpace;
            } else if (ret == SOAPY_SDR_UNDERFLOW) {
                underflow_count++;
                if (underflow_count % 100 == 0) {
                    std::cerr << "SinkSoapySDRBlock: Underflow count: " << underflow_count << std::endl;
                }
                samples_sent += to_send;
            } else if (ret < 0) {
                std::cerr << "SinkSoapySDRBlock: writeStream error: " << SoapySDR::errToStr(ret) << std::endl;
                in.commit_read(samples_sent);
                return cler::Error::TERM_ProcedureError;
            } else {
                samples_sent += ret;
            }
        }
        
        in.commit_read(samples_sent);
        return cler::Empty{};
    }
    
    void set_frequency(double freq) {
        try {
            device->setFrequency(SOAPY_SDR_TX, channel_idx, freq);
            center_freq = freq;
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_frequency failed: " << e.what() << std::endl;
        }
    }

    void set_gain(double gain) {
        try {
            device->setGain(SOAPY_SDR_TX, channel_idx, gain);
            gain_db = gain;
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_gain failed: " << e.what() << std::endl;
        }
    }

    void set_sample_rate(double rate) {
        try {
            device->setSampleRate(SOAPY_SDR_TX, channel_idx, rate);
            sample_rate = rate;
            if (device->getBandwidthRange(SOAPY_SDR_TX, channel_idx).size() > 0) {
                device->setBandwidth(SOAPY_SDR_TX, channel_idx, rate);
            }
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_sample_rate failed: " << e.what() << std::endl;
        }
    }

    void set_bandwidth(double bw) {
        try {
            device->setBandwidth(SOAPY_SDR_TX, channel_idx, bw);
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_bandwidth failed: " << e.what() << std::endl;
        }
    }

    void set_antenna(const std::string& antenna) {
        try {
            device->setAntenna(SOAPY_SDR_TX, channel_idx, antenna);
        } catch (const std::exception& e) {
            std::cerr << "SinkSoapySDRBlock: set_antenna failed: " << e.what() << std::endl;
        }
    }
    
    void set_dc_offset(const std::complex<double>& offset) {
        if (device->hasDCOffset(SOAPY_SDR_TX, channel_idx)) {
            device->setDCOffset(SOAPY_SDR_TX, channel_idx, offset);
        }
    }
    
    void set_iq_balance(const std::complex<double>& balance) {
        if (device->hasIQBalance(SOAPY_SDR_TX, channel_idx)) {
            device->setIQBalance(SOAPY_SDR_TX, channel_idx, balance);
        }
    }
    
    double get_frequency() const { return center_freq; }
    double get_gain() const { return gain_db; }
    double get_sample_rate() const { return sample_rate; }
    
    double get_bandwidth() const {
        return device->getBandwidth(SOAPY_SDR_TX, channel_idx);
    }
    
    std::string get_antenna() const {
        return device->getAntenna(SOAPY_SDR_TX, channel_idx);
    }
    
    std::vector<std::string> list_antennas() const {
        return device->listAntennas(SOAPY_SDR_TX, channel_idx);
    }
    
    SoapySDR::RangeList get_frequency_range() const {
        return device->getFrequencyRange(SOAPY_SDR_TX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range() const {
        return device->getGainRange(SOAPY_SDR_TX, channel_idx);
    }
    
    std::vector<std::string> list_gains() const {
        return device->listGains(SOAPY_SDR_TX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range(const std::string& name) const {
        return device->getGainRange(SOAPY_SDR_TX, channel_idx, name);
    }
    
    SoapySDR::RangeList get_sample_rate_range() const {
        return device->getSampleRateRange(SOAPY_SDR_TX, channel_idx);
    }
    
private:
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t channel_idx;

    SoapySDR::Device* device;
    SoapySDR::Stream* stream;

    std::vector<T> buffer;
    size_t mtu;

    size_t underflow_count = 0;
};

using SinkSoapySDRBlockCF32 = SinkSoapySDRBlock<std::complex<float>>;
using SinkSoapySDRBlockCS16 = SinkSoapySDRBlock<std::complex<int16_t>>;
using SinkSoapySDRBlockCS8 = SinkSoapySDRBlock<std::complex<int8_t>>;
using SinkSoapySDRBlockCU8 = SinkSoapySDRBlock<std::complex<uint8_t>>;
using SinkSoapySDRBlockS32 = SinkSoapySDRBlock<int32_t>;
using SinkSoapySDRBlockS16 = SinkSoapySDRBlock<int16_t>;
using SinkSoapySDRBlockU8 = SinkSoapySDRBlock<uint8_t>;
using SinkSoapySDRBlockF32 = SinkSoapySDRBlock<float>;