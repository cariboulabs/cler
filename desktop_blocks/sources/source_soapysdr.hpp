#pragma once

#include "cler.hpp"
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Errors.hpp>
#include <complex>
#include <vector>
#include <string>
#include <iostream>
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
struct SourceSoapySDRBlock : public cler::BlockBase {
    
    SourceSoapySDRBlock(const char* name, 
                        const std::string& args,
                        double freq,
                        double rate,
                        double gain = 20.0,
                        size_t channel = 0)
        : BlockBase(name),
          device_args(args),
          center_freq(freq),
          sample_rate(rate),
          gain_db(gain),
          channel_idx(channel),
          device(nullptr),
          stream(nullptr) {
        
        // Create device
        device = SoapySDR::Device::make(device_args);
        if (!device) {
            throw std::runtime_error("SourceSoapySDRBlock: Failed to create SoapySDR device with args: " + device_args);
        }
        
        // Validate and set sample rate
        auto sample_rates = device->getSampleRateRange(SOAPY_SDR_RX, channel_idx);
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
            throw std::runtime_error(ss.str());
        }
        device->setSampleRate(SOAPY_SDR_RX, channel_idx, sample_rate);
        
        // Validate and set center frequency
        auto freq_ranges = device->getFrequencyRange(SOAPY_SDR_RX, channel_idx);
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
            throw std::runtime_error(ss.str());
        }
        device->setFrequency(SOAPY_SDR_RX, channel_idx, center_freq);
        
        // Validate and set gain
        auto gain_range = device->getGainRange(SOAPY_SDR_RX, channel_idx);
        if (gain_db < gain_range.minimum() || gain_db > gain_range.maximum()) {
            std::stringstream ss;
            ss << "Gain " << gain_db << " dB not supported. Supported range: "
               << gain_range.minimum() << "-" << gain_range.maximum() << " dB";
            SoapySDR::Device::unmake(device);
            throw std::runtime_error(ss.str());
        }
        if (device->hasGainMode(SOAPY_SDR_RX, channel_idx)) {
            device->setGainMode(SOAPY_SDR_RX, channel_idx, false); // Manual gain mode
        }
        device->setGain(SOAPY_SDR_RX, channel_idx, gain_db);
        
        // Set bandwidth to match sample rate (if supported)
        if (device->getBandwidthRange(SOAPY_SDR_RX, channel_idx).size() > 0) {
            device->setBandwidth(SOAPY_SDR_RX, channel_idx, sample_rate);
        }
        
        // Setup stream with appropriate format
        std::vector<size_t> channels = {channel_idx};
        std::string format = get_soapy_format<T>();
        
        stream = device->setupStream(SOAPY_SDR_RX, format, channels);
        if (!stream) {
            SoapySDR::Device::unmake(device);
            throw std::runtime_error("SourceSoapySDRBlock: Failed to setup RX stream");
        }
        
        // Get MTU
        mtu = device->getStreamMTU(stream);
        
        // Activate stream
        int ret = device->activateStream(stream);
        if (ret != 0) {
            device->closeStream(stream);
            SoapySDR::Device::unmake(device);
            throw std::runtime_error("SourceSoapySDRBlock: Failed to activate stream: " + std::string(SoapySDR::errToStr(ret)));
        }
        
        // Print device info
        std::cout << "SourceSoapySDRBlock: Initialized " << device->getDriverKey() 
                  << " (" << device->getHardwareKey() << ")"
                  << " at " << center_freq/1e6 << " MHz"
                  << ", " << sample_rate/1e6 << " MSPS"
                  << ", " << gain_db << " dB gain"
                  << ", MTU: " << mtu << " samples" << std::endl;
                  
        // Print available antennas
        auto antennas = device->listAntennas(SOAPY_SDR_RX, channel_idx);
        if (!antennas.empty()) {
            std::cout << "  Available RX antennas: ";
            for (const auto& ant : antennas) {
                std::cout << ant << " ";
            }
            std::cout << std::endl;
        }
    }
    
    ~SourceSoapySDRBlock() {
        if (stream && device) {
            device->deactivateStream(stream);
            device->closeStream(stream);
        }
        if (device) {
            SoapySDR::Device::unmake(device);
        }
    }
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Get output buffer using DBF
        auto [write_ptr, write_size] = out->write_dbf();
        if (write_ptr == nullptr || write_size == 0) {
            return cler::Error::NotEnoughSpace;
        }
        
        // Read directly into output buffer
        size_t to_read = std::min(write_size, mtu);
        void* buffs[] = {write_ptr};
        int flags = 0;
        long long time_ns = 0;
        const long timeout_us = 100000; // 100ms timeout
        
        int ret = device->readStream(stream, buffs, to_read, flags, time_ns, timeout_us);
        
        if (ret > 0) {
            out->commit_write(ret);
            return cler::Empty{};
        } else if (ret == SOAPY_SDR_TIMEOUT) {
            return cler::Error::NotEnoughSamples;
        } else if (ret == SOAPY_SDR_OVERFLOW) {
            overflow_count++;
            if (overflow_count % 100 == 0) {
                std::cerr << "SourceSoapySDRBlock: Overflow count: " << overflow_count << std::endl;
            }
            return cler::Empty{}; // Continue despite overflow
        } else {
            std::cerr << "SourceSoapySDRBlock: readStream error: " << SoapySDR::errToStr(ret) << std::endl;
            return cler::Error::TERM_ProcedureError;
        }
    }
    
    // Control methods
    void set_frequency(double freq) {
        device->setFrequency(SOAPY_SDR_RX, channel_idx, freq);
        center_freq = freq;
    }
    
    void set_gain(double gain) {
        device->setGain(SOAPY_SDR_RX, channel_idx, gain);
        gain_db = gain;
    }

    void set_gain_element(const std::string& name, double gain) {
        device->setGain(SOAPY_SDR_RX, channel_idx, name, gain);
    }
    
    void set_sample_rate(double rate) {
        device->setSampleRate(SOAPY_SDR_RX, channel_idx, rate);
        sample_rate = rate;
        // Also update bandwidth to match
        if (device->getBandwidthRange(SOAPY_SDR_RX, channel_idx).size() > 0) {
            device->setBandwidth(SOAPY_SDR_RX, channel_idx, rate);
        }
    }
    
    void set_bandwidth(double bw) {
        device->setBandwidth(SOAPY_SDR_RX, channel_idx, bw);
    }
    
    void set_antenna(const std::string& antenna) {
        // Validate antenna name
        auto antennas = device->listAntennas(SOAPY_SDR_RX, channel_idx);
        if (std::find(antennas.begin(), antennas.end(), antenna) == antennas.end()) {
            std::stringstream ss;
            ss << "Antenna '" << antenna << "' not supported. Available antennas: ";
            for (const auto& ant : antennas) {
                ss << ant << " ";
            }
            throw std::runtime_error(ss.str());
        }
        device->setAntenna(SOAPY_SDR_RX, channel_idx, antenna);
    }
    
    void set_dc_offset_mode(bool automatic) {
        if (device->hasDCOffsetMode(SOAPY_SDR_RX, channel_idx)) {
            device->setDCOffsetMode(SOAPY_SDR_RX, channel_idx, automatic);
        }
    }
    
    void set_agc_mode(bool enable) {
        if (device->hasGainMode(SOAPY_SDR_RX, channel_idx)) {
            device->setGainMode(SOAPY_SDR_RX, channel_idx, enable);
        }
    }
    
    // Getters
    double get_frequency() const { return center_freq; }
    double get_gain() const { return gain_db; }
    double get_sample_rate() const { return sample_rate; }
    
    double get_bandwidth() const {
        return device->getBandwidth(SOAPY_SDR_RX, channel_idx);
    }
    
    std::string get_antenna() const {
        return device->getAntenna(SOAPY_SDR_RX, channel_idx);
    }
    
    std::vector<std::string> list_antennas() const {
        return device->listAntennas(SOAPY_SDR_RX, channel_idx);
    }
    
    SoapySDR::RangeList get_frequency_range() const {
        return device->getFrequencyRange(SOAPY_SDR_RX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range() const {
        return device->getGainRange(SOAPY_SDR_RX, channel_idx);
    }
    
    std::vector<std::string> list_gains() const {
        return device->listGains(SOAPY_SDR_RX, channel_idx);
    }
    
    SoapySDR::Range get_gain_range(const std::string& name) const {
        return device->getGainRange(SOAPY_SDR_RX, channel_idx, name);
    }
    
    SoapySDR::RangeList get_sample_rate_range() const {
        return device->getSampleRateRange(SOAPY_SDR_RX, channel_idx);
    }
    
private:
    // Configuration
    std::string device_args;
    double center_freq;
    double sample_rate;
    double gain_db;
    size_t channel_idx;
    
    // SoapySDR objects
    SoapySDR::Device* device;
    SoapySDR::Stream* stream;
    
    // Streaming
    size_t mtu;
    
    // Statistics
    size_t overflow_count = 0;
};

// Common template instantiations
using SourceSoapySDRBlockCF32 = SourceSoapySDRBlock<std::complex<float>>;
using SourceSoapySDRBlockCS16 = SourceSoapySDRBlock<std::complex<int16_t>>;
using SourceSoapySDRBlockCS8 = SourceSoapySDRBlock<std::complex<int8_t>>;
using SourceSoapySDRBlockCU8 = SourceSoapySDRBlock<std::complex<uint8_t>>;
using SourceSoapySDRBlockS32 = SourceSoapySDRBlock<int32_t>;
using SourceSoapySDRBlockS16 = SourceSoapySDRBlock<int16_t>;
using SourceSoapySDRBlockU8 = SourceSoapySDRBlock<uint8_t>;
using SourceSoapySDRBlockF32 = SourceSoapySDRBlock<float>;

// Helper function for device selection
struct SoapyDeviceInfo {
    std::string driver;
    std::string label;
    std::string serial;
    SoapySDR::Kwargs args;
    
    std::string get_args_string() const {
        return SoapySDR::KwargsToString(args);
    }
};

inline std::vector<SoapyDeviceInfo> enumerate_devices() {
    std::vector<SoapyDeviceInfo> devices;
    auto results = SoapySDR::Device::enumerate();
    
    for (const auto& result : results) {
        SoapyDeviceInfo info;
        info.args = result;
        
        // Extract key fields
        if (result.count("driver")) info.driver = result.at("driver");
        if (result.count("label")) info.label = result.at("label");
        if (result.count("serial")) info.serial = result.at("serial");
        
        devices.push_back(info);
    }
    
    return devices;
}