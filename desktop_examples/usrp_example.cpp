// Unified USRP Example - demonstrates all UHD block features
// Select mode via command line argument

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_uhd.hpp"
#include "desktop_blocks/sources/source_file.hpp"
#include "desktop_blocks/sinks/sink_uhd.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <iostream>
#include <vector>
#include <map>

void print_usage(const char* prog) {
    std::cout << "\nUSRP Example - Unified demonstration of UHD block features\n" << std::endl;
    std::cout << "Usage: " << prog << " <mode> [options...]" << std::endl;
    std::cout << "\nAvailable modes:" << std::endl;
    std::cout << "  list        - List available USRP devices" << std::endl;
    std::cout << "  rx          - Simple RX with spectrum plot" << std::endl;
    std::cout << "  tx-burst    - Timed TX burst transmission" << std::endl;
    std::cout << "  freq-hop    - Frequency hopping with timed commands" << std::endl;
    std::cout << "  gpio        - GPIO control with precise timing" << std::endl;
    std::cout << "  info        - Display detailed device information" << std::endl;
    std::cout << "\nMode-specific options:" << std::endl;
    std::cout << "  rx:         [device_args] [freq_hz] [rate_hz] [gain_db]" << std::endl;
    std::cout << "  tx-burst:   [device_args] [filename] [freq_hz] [rate_hz] [gain_db] [tx_time_s]" << std::endl;
    std::cout << "  freq-hop:   [device_args] [base_freq_hz] [hop_interval_s] [num_hops]" << std::endl;
    std::cout << "  gpio:       [device_args] [gpio_bank]" << std::endl;
    std::cout << "  info:       [device_args]" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << " list" << std::endl;
    std::cout << "  " << prog << " rx \"addr=192.168.10.2\" 915e6 2e6 30" << std::endl;
    std::cout << "  " << prog << " tx-burst \"addr=192.168.10.2\" burst.bin 915e6 2e6 0 2.0" << std::endl;
    std::cout << "  " << prog << " freq-hop \"addr=192.168.10.2\" 900e6 0.1 10" << std::endl;
    std::cout << "  " << prog << " gpio \"addr=192.168.10.2\" FP0" << std::endl;
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

    SourceUHDBlock<std::complex<float>> usrp("USRP", device_args, freq, rate, gain);  // num_channels defaults to 1
    PlotCSpectrumBlock spectrum("USRP Spectrum", {"I/Q"}, rate, 2048, 10);
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

void mode_tx_burst(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";
    std::string filename = argc > 3 ? argv[3] : "burst.bin";
    double freq = argc > 4 ? std::stod(argv[4]) : 915e6;
    double rate = argc > 5 ? std::stod(argv[5]) : 2e6;
    double gain = argc > 6 ? std::stod(argv[6]) : 0.0;
    double tx_time = argc > 7 ? std::stod(argv[7]) : 2.0;

    std::cout << "TX Burst Mode" << std::endl;
    std::cout << "Device: " << (device_args.empty() ? "default" : device_args) << std::endl;
    std::cout << "File: " << filename << std::endl;
    std::cout << "Freq: " << freq/1e6 << " MHz, Rate: " << rate/1e6 << " MSPS, Gain: " << gain << " dB" << std::endl;
    std::cout << "TX Time: " << tx_time << "s" << std::endl;

    SourceFileBlock<std::complex<float>> file("File", filename);
    SinkUHDBlock<std::complex<float>> usrp("USRP", device_args, freq, rate, gain, 0);

    usrp.set_time_now(0.0);

    TxMetadata md;
    md.has_time_spec = true;
    md.time_seconds = tx_time;
    md.time_frac_seconds = 0.0;
    md.start_of_burst = true;
    md.end_of_burst = true;
    usrp.set_tx_metadata(md);

    std::cout << "Burst configured for t=" << tx_time << "s" << std::endl;

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&file, &usrp.in),
        cler::BlockRunner(&usrp)
    );

    std::cout << "Starting transmission..." << std::endl;
    flowgraph.run();

    while (usrp.get_time_now() < tx_time + 1.0) {
        AsyncTxEvent event;
        if (usrp.poll_async_event(event, 0.0)) {
            if (event.event_code == uhd::async_metadata_t::EVENT_CODE_BURST_ACK) {
                std::cout << "Burst ACK at t=" << event.time_seconds + event.time_frac_seconds << "s" << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    flowgraph.stop();
    std::cout << "Underflows: " << usrp.get_underflow_count() << std::endl;
}

void mode_freq_hop(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";
    double base_freq = argc > 3 ? std::stod(argv[3]) : 900e6;
    double hop_interval = argc > 4 ? std::stod(argv[4]) : 0.1;
    int num_hops = argc > 5 ? std::stoi(argv[5]) : 10;

    std::cout << "Frequency Hopping Mode" << std::endl;
    std::cout << "Base Freq: " << base_freq/1e6 << " MHz, Interval: " << hop_interval << "s, Hops: " << num_hops << std::endl;

    std::vector<double> frequencies;
    for (int i = 0; i < num_hops; ++i) {
        frequencies.push_back(base_freq + (i * 10e6));
    }

    std::cout << "Hopping sequence: ";
    for (auto f : frequencies) std::cout << f/1e6 << " ";
    std::cout << "MHz" << std::endl;

    SourceUHDBlock<std::complex<float>> usrp("USRP", device_args, frequencies[0], 2e6, 30.0, 0);
    SinkNullBlock<std::complex<float>> null_sink("Null");

    usrp.set_time_now(0.0);

    std::cout << "Scheduling hops..." << std::endl;
    for (size_t i = 0; i < frequencies.size(); ++i) {
        double hop_time = i * hop_interval;
        usrp.set_command_time(hop_time);
        usrp.set_frequency_timed(frequencies[i]);
    }

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp, &null_sink.in),
        cler::BlockRunner(&null_sink)
    );

    flowgraph.run();

    double total_time = num_hops * hop_interval + 0.5;
    auto start = std::chrono::steady_clock::now();

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() / 1000.0;
        if (elapsed >= total_time) break;

        double usrp_time = usrp.get_time_now();
        int current_hop = static_cast<int>(usrp_time / hop_interval);
        if (current_hop < num_hops) {
            std::cout << "\rTime: " << usrp_time << "s | Hop: " << current_hop
                      << " | Freq: " << frequencies[current_hop]/1e6 << " MHz     " << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << std::endl;
    flowgraph.stop();
}

void mode_gpio(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";
    std::string gpio_bank = argc > 3 ? argv[3] : "FP0";

    std::cout << "GPIO Trigger Mode" << std::endl;
    std::cout << "GPIO Bank: " << gpio_bank << " (device-specific)" << std::endl;

    SourceUHDBlock<std::complex<float>> usrp("USRP", device_args, 915e6, 2e6, 30.0, 0);
    SinkNullBlock<std::complex<float>> null_sink("Null");

    std::cout << "Configuring GPIO..." << std::endl;
    usrp.gpio_set_ddr(gpio_bank, 0xFF, 0xFF);
    usrp.gpio_set_ctrl(gpio_bank, 0x00, 0xFF);
    usrp.gpio_set_out(gpio_bank, 0x00, 0xFF);

    usrp.set_time_now(0.0);

    std::cout << "Scheduling GPIO toggles..." << std::endl;
    for (int i = 0; i < 5; ++i) {
        double toggle_time = 1.0 + (i * 0.5);
        uint32_t value = (i % 2 == 0) ? 0xFF : 0x00;
        usrp.set_command_time(toggle_time);
        usrp.gpio_set_out_timed(gpio_bank, value, 0xFF);
        std::cout << "  t=" << toggle_time << "s: 0x" << std::hex << value << std::dec << std::endl;
    }

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&usrp, &null_sink.in),
        cler::BlockRunner(&null_sink)
    );

    flowgraph.run();

    while (usrp.get_time_now() < 4.0) {
        uint32_t gpio_state = usrp.gpio_get_in(gpio_bank);
        std::cout << "\rTime: " << usrp.get_time_now() << "s | GPIO: 0x"
                  << std::hex << gpio_state << std::dec << "     " << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << std::endl;
    flowgraph.stop();
    usrp.gpio_set_out(gpio_bank, 0x00, 0xFF);
}

void mode_info(int argc, char** argv) {
    std::string device_args = argc > 2 ? argv[2] : "";

    std::cout << "Device Information Mode" << std::endl;

    SourceUHDBlock<std::complex<float>> usrp("USRP", device_args, 915e6, 2e6, 30.0, 0);

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
        } else if (mode == "tx-burst") {
            mode_tx_burst(argc, argv);
        } else if (mode == "freq-hop") {
            mode_freq_hop(argc, argv);
        } else if (mode == "gpio") {
            mode_gpio(argc, argv);
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
