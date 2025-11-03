// HackRF CW (Continuous Wave) Example - Transmit a single tone
// Shows spectrum plot and transmits via HackRF

#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_cw.hpp"
#include "desktop_blocks/sinks/sink_hackrf.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/utils/fanout.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <iostream>
#include <chrono>
#include <thread>

void print_usage(const char* prog) {
    std::cout << "\nHackRF CW Example - Transmit Continuous Wave\n" << std::endl;
    std::cout << "Usage: " << prog << " [freq_mhz] [sample_rate_msps] [cw_offset_khz] [amplitude] [txvga_gain_db] [amp_enable]" << std::endl;
    std::cout << "\nParameters:" << std::endl;
    std::cout << "  freq_mhz         - TX center frequency in MHz (default: 915)" << std::endl;
    std::cout << "  sample_rate_msps - Sample rate in MSPS (default: 2)" << std::endl;
    std::cout << "  cw_offset_khz    - CW tone offset from center in kHz (default: 100)" << std::endl;
    std::cout << "  amplitude        - Signal amplitude 0.0-1.0 (default: 0.5)" << std::endl;
    std::cout << "  txvga_gain_db    - TX VGA gain 0-47 dB (default: 20)" << std::endl;
    std::cout << "  amp_enable       - Enable TX amp: 0 or 1 (default: 0)" << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  " << prog << std::endl;
    std::cout << "  " << prog << " 915 2 100 0.5 20 0" << std::endl;
    std::cout << "  " << prog << " 433 4 0 0.3 30 0    # Tone at center frequency" << std::endl;
    std::cout << "  " << prog << " 915 2 250 0.7 25 1  # With amp enabled" << std::endl;
    std::cout << "\nWarning: Ensure you have proper licensing and are using appropriate" << std::endl;
    std::cout << "frequencies for your region. This transmits a continuous carrier!" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char** argv) {
    // Parse command line arguments
    double freq_mhz = 915.0;
    double sample_rate_msps = 2.0;
    double cw_offset_khz = 100.0;
    float amplitude = 0.5f;
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
        cw_offset_khz = std::stod(argv[3]);
    }
    if (argc > 4) {
        amplitude = std::stof(argv[4]);
        if (amplitude < 0.0f || amplitude > 1.0f) {
            std::cerr << "Error: Amplitude must be 0.0-1.0" << std::endl;
            return 1;
        }
    }
    if (argc > 5) {
        txvga_gain_db = std::stoi(argv[5]);
        if (txvga_gain_db < 0 || txvga_gain_db > 47) {
            std::cerr << "Error: TXVGA gain must be 0-47 dB" << std::endl;
            return 1;
        }
    }
    if (argc > 6) {
        amp_enable = (std::stoi(argv[6]) != 0);
    }

    uint64_t freq_hz = static_cast<uint64_t>(freq_mhz * 1e6);
    uint32_t sample_rate_hz = static_cast<uint32_t>(sample_rate_msps * 1e6);
    double cw_freq_hz = cw_offset_khz * 1e3;

    std::cout << "HackRF CW Example - Continuous Wave" << std::endl;
    std::cout << "====================================" << std::endl;
    std::cout << "TX Center Frequency: " << freq_mhz << " MHz" << std::endl;
    std::cout << "CW Tone Offset: " << cw_offset_khz << " kHz" << std::endl;
    std::cout << "Actual TX Frequency: " << (freq_mhz + cw_offset_khz/1000.0) << " MHz" << std::endl;
    std::cout << "Sample Rate: " << sample_rate_msps << " MSPS" << std::endl;
    std::cout << "Amplitude: " << amplitude << std::endl;
    std::cout << "TXVGA Gain: " << txvga_gain_db << " dB" << std::endl;
    std::cout << "Amp Enable: " << (amp_enable ? "Yes" : "No") << std::endl;
    std::cout << std::endl;

    try {
        // Create GUI window
        cler::GuiManager gui(1200, 600, "HackRF TX - Continuous Wave");

        // Create blocks
        // CW source: single tone at specified offset frequency
        SourceCWBlock<std::complex<float>> cw_source("CW", 
            amplitude,           // Amplitude
            cw_freq_hz,         // Frequency offset from center
            sample_rate_hz);    // Sample rate

        // Fanout to send CW to both spectrum plot and HackRF
        FanoutBlock<std::complex<float>> fanout("Fanout", 2);

        // Spectrum plot to visualize the CW tone
        PlotCSpectrumBlock spectrum("TX Spectrum", {"CW Tone"}, sample_rate_hz, 2048);
        spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

        // HackRF TX sink
        SinkHackRFBlock hackrf_tx("HackRF_TX", freq_hz, sample_rate_hz, txvga_gain_db, amp_enable);

        // Build flowgraph
        auto flowgraph = cler::make_desktop_flowgraph(
            cler::BlockRunner(&cw_source, &fanout.in),
            cler::BlockRunner(&fanout, &spectrum.in[0], &hackrf_tx.in),
            cler::BlockRunner(&spectrum),
            cler::BlockRunner(&hackrf_tx)
        );

        std::cout << "Starting flowgraph..." << std::endl;
        flowgraph.run();
        std::cout << "Transmitting CW tone. Close window to stop." << std::endl;
        std::cout << "You should see a single spectral line at " << cw_offset_khz << " kHz offset." << std::endl;
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
            std::cout << "This is unusual for CW and may indicate system issues." << std::endl;
        }

        std::cout << "Done." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}