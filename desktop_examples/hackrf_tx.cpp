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

void print_usage(const char* prog) {
    std::cout << "\nHackRF TX Example - Transmit Chirp Signal\n" << std::endl;
    std::cout << "Usage: " << prog << " [freq_mhz] [sample_rate_msps] [txvga_gain_db] [amp_enable]" << std::endl;
    std::cout << "\nParameters:" << std::endl;
    std::cout << "  freq_mhz         - TX frequency in MHz (default: 915)" << std::endl;
    std::cout << "  sample_rate_msps - Sample rate in MSPS (default: 2)" << std::endl;
    std::cout << "  txvga_gain_db    - TX VGA gain 0-47 dB (default: 20)" << std::endl;
    std::cout << "  amp_enable       - Enable TX amp: 0 or 1 (default: 0)" << std::endl;
    std::cout << "\nChirp parameters:" << std::endl;
    std::cout << "  Amplitude: 0.3 (to prevent clipping)" << std::endl;
    std::cout << "  Sweep: -500 kHz to +500 kHz over 1 second" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << std::endl;
    std::cout << "  " << prog << " 915 2 20 0" << std::endl;
    std::cout << "  " << prog << " 433 4 30 1  # 433 MHz with amp enabled" << std::endl;
    std::cout << "\nWarning: Ensure you have proper licensing and are using appropriate" << std::endl;
    std::cout << "frequencies for your region. TX amplifier adds ~10dB but increases harmonics." << std::endl;
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    // Parse command line arguments
    double freq_mhz = 915.0;
    double sample_rate_msps = 2.0;
    int txvga_gain_db = 47;
    bool amp_enable = false;

    if (argc > 1) {
        if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        freq_mhz = std::stod(argv[1]);
    }
    if (argc > 2) {
        sample_rate_msps = std::stod(argv[2]);
    }
    if (argc > 3) {
        txvga_gain_db = std::stoi(argv[3]);
        if (txvga_gain_db < 0 || txvga_gain_db > 47) {
            std::cerr << "Error: TXVGA gain must be 0-47 dB" << std::endl;
            return 1;
        }
    }
    if (argc > 4) {
        amp_enable = (std::stoi(argv[4]) != 0);
    }

    uint64_t freq_hz = static_cast<uint64_t>(freq_mhz * 1e6);
    uint32_t sample_rate_hz = static_cast<uint32_t>(sample_rate_msps * 1e6);

    std::cout << "HackRF TX Example - Chirp Signal" << std::endl;
    std::cout << "=================================" << std::endl;
    std::cout << "TX Frequency: " << freq_mhz << " MHz" << std::endl;
    std::cout << "Sample Rate: " << sample_rate_msps << " MSPS" << std::endl;
    std::cout << "TXVGA Gain: " << txvga_gain_db << " dB" << std::endl;
    std::cout << "Amp Enable: " << (amp_enable ? "Yes" : "No") << std::endl;
    std::cout << std::endl;

    try {
        // Create GUI window
        cler::GuiManager gui(1200, 600, "HackRF TX - Chirp Signal");

        // Create blocks
        // Chirp: -500 kHz to +500 kHz over 1 second, amplitude 0.3 to prevent clipping
        SourceChirpBlock<std::complex<float>> chirp("Chirp", 
            0.3f,                    // Amplitude (reduced to prevent clipping)
            -500e3f,                 // Start frequency: -500 kHz
            500e3f,                  // End frequency: +500 kHz  
            sample_rate_hz,          // Sample rate
            0.1f);                   // Duration: 1 second

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