#pragma once
#include <string>

struct UHDConfig {
    double center_freq_Hz = 915e6;
    double sample_rate_Hz = 2e6;
    double gain = 40.0;
    double bandwidth_Hz = 4e6;
};

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
