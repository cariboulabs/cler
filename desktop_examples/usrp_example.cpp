// Unified USRP Example - demonstrates all UHD block features including TX
// Select mode via command line argument

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd_zohar_full.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sinks/sink_uhd.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <chrono>
#include <iostream>
#include <vector>
#include <map>

void print_usage(const char* prog) {
    std::cout << "\nUSRP Example - Unified demonstration of UHD block features\n" << std::endl;
    std::cout << "Usage: " << prog << " <mode> [options...]" << std::endl;
    std::cout << "\nAvailable modes:" << std::endl;
    std::cout << "  list        - List available USRP devices" << std::endl;
    std::cout << "  rx          - Simple RX with spectrum plot" << std::endl;
    std::cout << "  tx-chirp    - Transmit chirp signal with spectrum plot" << std::endl;
    std::cout << "  tx-cw       - Transmit continuous wave with spectrum plot" << std::endl;
    std::cout << "  info        - Display detailed device information" << std::endl;
    std::cout << "\nMode-specific options:" << std::endl;
    std::cout << "  rx:         [device_args] [freq_hz] [rate_hz] [gain_db]" << std::endl;
    std::cout << "  tx-chirp:   [device_args] [freq_hz] [rate_hz] [gain_db] [amplitude]" << std::endl;
    std::cout << "  tx-cw:      [device_args] [freq_hz] [rate_hz] [gain_db] [cw_offset_hz] [amplitude]" << std::endl;
    std::cout << "  info:       [device_args]" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << " list" << std::endl;
    std::cout << "  " << prog << " rx \"addr=192.168.10.2\" 915e6 2e6 30" << std::endl;
    std::cout << "  " << prog << " tx-chirp \"addr=192.168.10.2\" 915e6 2e6 0 0.3" << std::endl;
    std::cout << "  " << prog << " tx-cw \"addr=192.168.10.2\" 915e6 2e6 0 100e3 0.5" << std::endl;
    std::cout << std::endl;
}

void mode_list() {
    std::cout << "Enumerating USRP devices..." << std::endl;
    auto devices = enumerate_usrp_devices();

    if (devices.empty()) {
        std::cout << "No USRP devices found!" << std::endl;
        return;
    }

    std::cout << "Found " << devices.size() << " device(s):" << std::endl;
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "\n[" << i << "] " << devices[i].product << std::endl;
        std::cout << "  Type:   " << devices[i].type << std::endl;
        std::cout << "  Serial: " << devices[i].serial << std::endl;
        std::cout << "  Name:   " << devices[i].name << std::endl;
        std::cout << "  Args:   " << devices[i].get_args_string() << std::endl;
    }
}

void mode_rx(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";
    double freq = argc > 3 ? std::stod(argv[3]) : 915e6;
    double rate = argc > 4 ? std::stod(argv[4]) : 2e6;
    double gain = argc > 5 ? std::stod(argv[5]) : 30.0;

    std::cout << "RX Mode - Spectrum Plot" << std::endl;
    std::cout << "Device: " << (device_args.empty() ? "default" : device_args) << std::endl;
    std::cout << "Freq: " << freq/1e6 << " MHz, Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;

    cler::GuiManager gui(1200, 600, "USRP RX - Spectrum");

    SourceUHDBlock<std::complex<float>> usrp("USRP", device_args, freq, rate, gain, 1);
    PlotCSpectrumBlock spectrum("USRP Spectrum", {"I/Q"}, rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp, &spectrum.in[0]),
        cler::BlockRunner(&spectrum)
    );

    flowgraph.run();
    std::cout << "Flowgraph running... Close window to exit." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Overflows: " << usrp.get_overflow_count() << std::endl;
}

void mode_tx_chirp(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";
    double freq = argc > 3 ? std::stod(argv[3]) : 915e6;
    double rate = argc > 4 ? std::stod(argv[4]) : 2e6;
    double gain = argc > 5 ? std::stod(argv[5]) : 0.0;
    float amplitude = argc > 6 ? std::stof(argv[6]) : 0.3f;
    float chirp_duration = argc > 7 ? std::stof(argv[7]) : 1.0f;
    std::cout << "TX Chirp Mode" << std::endl;
    std::cout << "Device: " << (device_args.empty() ? "default" : device_args) << std::endl;
    std::cout << "Freq: " << freq/1e6 << " MHz, Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;
    std::cout << "Amplitude: " << amplitude << std::endl;
    std::cout << "Chirp: -500 kHz to +500 kHz over 1 second" << std::endl;

    cler::GuiManager gui(1200, 600, "USRP TX - Chirp Signal");

    // Chirp source: -500 kHz to +500 kHz over 1 second
    SourceChirpBlock<std::complex<float>> chirp("Chirp", 
        amplitude,       // Amplitude
        -500e3f,         // Start frequency
        500e3f,          // End frequency
        rate,            // Sample rate
        chirp_duration); // Duration

    // Fanout to spectrum plot and USRP
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    // Spectrum plot
    PlotCSpectrumBlock spectrum("TX Spectrum", {"Chirp"}, rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    // USRP TX sink
    SinkUHDBlock<std::complex<float>> usrp("USRP_TX", device_args, freq, rate, gain, 1);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&chirp, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp)
    );

    flowgraph.run();
    std::cout << "Transmitting chirp signal. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp.get_underflow_count() << std::endl;
}

void mode_tx_cw(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";
    double freq = argc > 3 ? std::stod(argv[3]) : 915e6;
    double rate = argc > 4 ? std::stod(argv[4]) : 2e6;
    double gain = argc > 5 ? std::stod(argv[5]) : 0.0;
    double cw_offset = argc > 6 ? std::stod(argv[6]) : 100e3;
    float amplitude = argc > 7 ? std::stof(argv[7]) : 0.5f;

    std::cout << "TX CW Mode - Continuous Wave" << std::endl;
    std::cout << "Device: " << (device_args.empty() ? "default" : device_args) << std::endl;
    std::cout << "Center Freq: " << freq/1e6 << " MHz" << std::endl;
    std::cout << "CW Offset: " << cw_offset/1e3 << " kHz" << std::endl;
    std::cout << "Actual TX: " << (freq + cw_offset)/1e6 << " MHz" << std::endl;
    std::cout << "Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;
    std::cout << "Amplitude: " << amplitude << std::endl;

    cler::GuiManager gui(1200, 600, "USRP TX - Continuous Wave");

    // CW source
    SourceCWBlock<std::complex<float>> cw("CW", 
        amplitude,      // Amplitude
        cw_offset,      // Frequency offset
        rate);          // Sample rate

    // Fanout to spectrum plot and USRP
    FanoutBlock<std::complex<float>> fanout("Fanout", 2);

    // Spectrum plot
    PlotCSpectrumBlock spectrum("TX Spectrum", {"CW Tone"}, rate, 2048);
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    // USRP TX sink
    SinkUHDBlock<std::complex<float>> usrp("USRP_TX", device_args, freq, rate, gain, 1);

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&cw, &fanout.in),
        cler::BlockRunner(&fanout, &spectrum.in[0], &usrp.in[0]),
        cler::BlockRunner(&spectrum),
        cler::BlockRunner(&usrp)
    );

    flowgraph.run();
    std::cout << "Transmitting chirp signal. Close window to stop." << std::endl;

    while (!gui.should_close()) {
        gui.begin_frame();
        spectrum.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp.get_underflow_count() << std::endl;
}

void mode_info(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";

    std::cout << "Device Information Mode" << std::endl;

    SourceUHDBlock<std::complex<float>> usrp("USRP", device_args, 915e6, 2e6, 30.0, 1);

    std::cout << "\n=== Device Information ===" << std::endl;
    std::cout << "Motherboard: " << usrp.get_mboard_name() << std::endl;
    std::cout << "\n" << usrp.get_pp_string() << std::endl;

    std::cout << "\n=== RX Configuration ===" << std::endl;
    std::cout << "Frequency: " << usrp.get_frequency()/1e6 << " MHz" << std::endl;
    auto freq_range = usrp.get_frequency_range();
    std::cout << "  Range: " << freq_range.start()/1e6 << " - " << freq_range.stop()/1e6 << " MHz" << std::endl;

    std::cout << "\nGain: " << usrp.get_gain() << " dB" << std::endl;
    auto gain_range = usrp.get_gain_range();
    std::cout << "  Range: " << gain_range.start() << " - " << gain_range.stop() << " dB" << std::endl;

    std::cout << "\nSample Rate: " << usrp.get_sample_rate()/1e6 << " MSPS" << std::endl;

    std::cout << "\nAntennas: ";
    auto antennas = usrp.list_antennas();
    for (const auto& ant : antennas) std::cout << ant << " ";
    std::cout << "(using: " << usrp.get_antenna() << ")" << std::endl;

    std::cout << "\n=== Clock/Time ===" << std::endl;
    std::cout << "Clock sources: ";
    auto clk_srcs = usrp.get_clock_sources();
    for (const auto& src : clk_srcs) std::cout << src << " ";
    std::cout << std::endl;

    std::cout << "Time sources: ";
    auto time_srcs = usrp.get_time_sources();
    for (const auto& src : time_srcs) std::cout << src << " ";
    std::cout << std::endl;

    std::cout << "\n=== Sensors ===" << std::endl;
    auto rx_sensors = usrp.get_rx_sensor_names();
    if (!rx_sensors.empty()) {
        std::cout << "RX Sensors:" << std::endl;
        for (const auto& sensor : rx_sensors) {
            std::cout << "  " << sensor << ": " << usrp.get_rx_sensor(sensor) << std::endl;
        }
    }

    auto mb_sensors = usrp.get_mboard_sensor_names();
    if (!mb_sensors.empty()) {
        std::cout << "Motherboard Sensors:" << std::endl;
        for (const auto& sensor : mb_sensors) {
            std::cout << "  " << sensor << ": " << usrp.get_mboard_sensor(sensor) << std::endl;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "list") {
            mode_list();
        } else if (mode == "rx") {
            mode_rx(argc, argv);
        } else if (mode == "tx-chirp") {
            mode_tx_chirp(argc, argv);
        } else if (mode == "tx-cw") {
            mode_tx_cw(argc, argv);
        } else if (mode == "info") {
            mode_info(argc, argv);
        } else {
            std::cerr << "Unknown mode: " << mode << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}