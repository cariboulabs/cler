#pragma once

// Common definitions for USRP blocks
struct USRPConfig {
    double center_freq_Hz = 915e6;
    double sample_rate_Hz = 2e6;
    double gain = 40.0;
    double bandwidth_Hz = 4e6;
};