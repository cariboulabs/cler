// HackRF TX Example - Transmit a chirp signal
// Shows spectrum plot and transmits via HackRF

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_chirp.hpp"
#include "desktop_blocks/sinks/sink_hackrf.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <string>

void print_usage(const char* prog) {
    std::cout << "\nHackRF TX Example - Transmit Chirp Signal\n" << std::endl;
    std::cout << "Usage: " << prog << " [OPTIONS]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -f, --freq FREQ          TX frequency in MHz (default: 915)" << std::endl;
    std::cout << "  -s, --samplerate RATE    Sample rate in MSPS (default: 2)" << std::endl;
    std::cout << "  -g, --gain GAIN          TX VGA gain 0-47 dB (default: 47)" << std::endl;
    std::cout << "  -A, --amp                Enable TX amplifier (default: disabled)" << std::endl;
    std::cout << "  -a, --amplitude AMP      Signal amplitude 0.0-1.0 (default: 0.3)" << std::endl;
    std::cout << "  -S, --start START        Chirp start frequency offset in kHz (default: -500)" << std::endl;
    std::cout << "  -E, --end END            Chirp end frequency offset in kHz (default: 500)" << std::endl;
    std::cout << "  -d, --duration DUR       Chirp duration in seconds (default: 0.1)" << std::endl;
    std::cout << "  -h, --help               Show this help message" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << std::endl;
    std::cout << "  " << prog << " -f 915 -s 2 -g 20" << std::endl;
    std::cout << "  " << prog << " --freq 433 --samplerate 4 --gain 30 -A" << std::endl;
    std::cout << "  " << prog << " -f 915 -S -1000 -E 1000 -d 0.5  # 2 MHz sweep over 0.5s" << std::endl;
    std::cout << "  " << prog << " -f 2400 -a 0.5 -g 25 -A  # Higher amplitude with amp" << std::endl;
    std::cout << "\nWarning: Ensure you have proper licensing and are using appropriate" << std::endl;
    std::cout << "frequencies for your region. TX amplifier adds ~10dB but increases harmonics." << std::endl;
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    // Default values
    double freq_mhz = 915.0;
    double sample_rate_msps = 2.0;
    int txvga_gain_db = 47;
    bool amp_enable = false;
    float amplitude = 1.0f;
    double start_freq_khz = -500.0;
    double end_freq_khz = 500.0;
    float duration_s = 0.1f;

    // Parse command line flags
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-f" || arg == "--freq") {
            if (i + 1 < argc) {
                freq_mhz = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-s" || arg == "--samplerate") {
            if (i + 1 < argc) {
                sample_rate_msps = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-g" || arg == "--gain") {
            if (i + 1 < argc) {
                txvga_gain_db = std::stoi(argv[++i]);
                if (txvga_gain_db < 0 || txvga_gain_db > 47) {
                    std::cerr << "Error: TXVGA gain must be 0-47 dB" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-A" || arg == "--amp") {
            amp_enable = true;
        }
        else if (arg == "-a" || arg == "--amplitude") {
            if (i + 1 < argc) {
                amplitude = std::stof(argv[++i]);
                if (amplitude < 0.0f || amplitude > 1.0f) {
                    std::cerr << "Error: Amplitude must be 0.0-1.0" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-S" || arg == "--start") {
            if (i + 1 < argc) {
                start_freq_khz = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-E" || arg == "--end") {
            if (i + 1 < argc) {
                end_freq_khz = std::stod(argv[++i]);
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else if (arg == "-d" || arg == "--duration") {
            if (i + 1 < argc) {
                duration_s = std::stof(argv[++i]);
                if (duration_s <= 0.0f) {
                    std::cerr << "Error: Duration must be greater than 0" << std::endl;
                    return 1;
                }
            } else {
                std::cerr << "Error: " << arg << " requires a value" << std::endl;
                return 1;
            }
        }
        else {
            std::cerr << "Error: Unknown option '" << arg << "'" << std::endl;
            std::cerr << "Use -h or --help for usage information" << std::endl;
            return 1;
        }
    }

    uint64_t freq_hz = static_cast<uint64_t>(freq_mhz * 1e6);
    uint32_t sample_rate_hz = static_cast<uint32_t>(sample_rate_msps * 1e6);
    float start_freq_hz = static_cast<float>(start_freq_khz * 1e3);
    float end_freq_hz = static_cast<float>(end_freq_khz * 1e3);

    std::cout << "HackRF TX Example - Chirp Signal" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << "TX Frequency: " << freq_mhz << " MHz" << std::endl;
    std::cout << "Sample Rate: " << sample_rate_msps << " MSPS" << std::endl;
    std::cout << "TXVGA Gain: " << txvga_gain_db << " dB" << std::endl;
    std::cout << "Amp Enable: " << (amp_enable ? "Yes" : "No") << std::endl;
    std::cout << "Amplitude: " << amplitude << std::endl;
    std::cout << "Chirp Sweep: " << start_freq_khz << " kHz to " << end_freq_khz << " kHz" << std::endl;
    std::cout << "Chirp Duration: " << duration_s << " seconds" << std::endl;
    std::cout << "Sweep Bandwidth: " << (end_freq_khz - start_freq_khz) << " kHz" << std::endl;
    std::cout << std::endl;

    try {
        // Create GUI window
        cler::GuiManager gui(1200, 600, "HackRF TX - Chirp Signal");

        // Create blocks
        SourceChirpBlock<std::complex<float>> chirp("Chirp", 
            amplitude,               // Amplitude
            start_freq_hz,          // Start frequency
            end_freq_hz,            // End frequency
            sample_rate_hz,         // Sample rate
            duration_s);            // Duration

        // Fanout to send chirp to both spectrum plot and HackRF
        FanoutBlock<std::complex<float>> fanout("Fanout", 2);

        // Spectrum plot to visualize what we're transmitting
        PlotCSpectrumBlock spectrum("TX Spectrum", {"Chirp"}, sample_rate_hz, 2048);
        spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

        // HackRF TX sink
        SinkHackRFBlock hackrf_tx("HackRF_TX", freq_hz, sample_rate_hz, txvga_gain_db, amp_enable);

        // Build flowgraph
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&chirp, &fanout.in),
            cler::BlockRunner(&fanout, &spectrum.in[0], &hackrf_tx.in),
            cler::BlockRunner(&spectrum),
            cler::BlockRunner(&hackrf_tx)
        );

        std::cout << "Starting flowgraph..." << std::endl;
        flowgraph.run();
        std::cout << "Transmitting chirp signal. Close window to stop." << std::endl;
        std::cout << "You should see the chirp sweeping in the spectrum plot." << std::endl;
        std::cout << std::endl;

        // GUI event loop
        auto last_stats = std::chrono::steady_clock::now();
        while (!gui.should_close()) {
            gui.begin_frame();
            spectrum.render();
            gui.end_frame();

            // Print underrun stats every 5 seconds
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats).count();
            if (elapsed >= 5) {
                size_t underruns = hackrf_tx.get_underrun_count();
                if (underruns > 0) {
                    std::cout << "TX underruns: " << underruns << std::endl;
                }
                last_stats = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        std::cout << "\nStopping transmission..." << std::endl;
        flowgraph.stop();

        // Print final statistics
        size_t final_underruns = hackrf_tx.get_underrun_count();
        std::cout << "Total TX underruns: " << final_underruns << std::endl;
        
        if (final_underruns > 0) {
            std::cout << "\nNote: Underruns indicate the source couldn't keep up with TX rate." << std::endl;
            std::cout << "This is normal for initial startup but shouldn't persist." << std::endl;
        }

        std::cout << "Done." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}