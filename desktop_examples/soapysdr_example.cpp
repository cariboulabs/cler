#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/sources/source_soapysdr.hpp"
#include "desktop_blocks/plots/plot_cspectrum.hpp"
#include "desktop_blocks/gui/gui_manager.hpp"
#include <iostream>

void list_devices() {
    std::cout << "Available SoapySDR devices:\n";
    auto results = SoapySDR::Device::enumerate();
    
    if (results.empty()) {
        std::cout << "  No devices found!\n";
        std::cout << "  Make sure your SDR is connected and drivers are installed.\n";
        return;
    }
    
    for (size_t i = 0; i < results.size(); i++) {
        std::cout << "  Device " << i << ":\n";
        for (const auto& pair : results[i]) {
            std::cout << "    " << pair.first << " = " << pair.second << "\n";
        }
        std::cout << "\n";
    }
}

void print_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help                Show this help message\n";
    std::cout << "  --list                List available devices and exit\n";
    std::cout << "  --device ARGS         Device arguments (default: driver=rtlsdr)\n";
    std::cout << "  --freq MHz            Center frequency in MHz (default: 100.3)\n";
    std::cout << "  --gain dB             Gain in dB (default: 20)\n";
    std::cout << "  --rate MSPS           Sample rate in MSPS (default: 2.048)\n";
    std::cout << "  --antenna NAME        Select antenna\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " --device \"driver=rtlsdr\" --freq 100.3 --gain 20\n";
    std::cout << "  " << program_name << " --device \"driver=hackrf\" --freq 433.92 --gain 14\n";
    std::cout << "  " << program_name << " --device \"driver=lime\" --freq 1090 --gain 30\n";
    std::cout << "  " << program_name << " --list\n";
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    std::string device_args = "driver=rtlsdr";
    double freq_mhz = 100.3;  // Default FM radio frequency
    double sample_rate_msps = 2.048;
    double gain = 20.0;
    std::string antenna = "";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_help(argv[0]);
            return 0;
        } else if (arg == "--list") {
            list_devices();
            return 0;
        } else if (arg == "--device" && i + 1 < argc) {
            device_args = argv[++i];
        } else if (arg == "--freq" && i + 1 < argc) {
            freq_mhz = std::stod(argv[++i]);
        } else if (arg == "--gain" && i + 1 < argc) {
            gain = std::stod(argv[++i]);
        } else if (arg == "--rate" && i + 1 < argc) {
            sample_rate_msps = std::stod(argv[++i]);
        } else if (arg == "--antenna" && i + 1 < argc) {
            antenna = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_help(argv[0]);
            return 1;
        }
    }
    
    double sample_rate = sample_rate_msps * 1e6;
    
    std::cout << "\nStarting SoapySDR example with:\n";
    std::cout << "  Device: " << device_args << "\n";
    std::cout << "  Frequency: " << freq_mhz << " MHz\n";
    std::cout << "  Sample Rate: " << sample_rate/1e6 << " MSPS\n";
    std::cout << "  Gain: " << gain << " dB\n";
    if (!antenna.empty()) {
        std::cout << "  Antenna: " << antenna << "\n";
    }
    std::cout << "\n";
    
    // Create GUI
    cler::GuiManager gui(1200, 600, "CLER SoapySDR Example");
    
    // Create SDR source
    SourceSoapySDRBlock<std::complex<float>> sdr_source(
    "SDR_Source",
    device_args,
    freq_mhz * 1e6,  // Convert MHz to Hz
    sample_rate,
    gain
    );
    
    // Set antenna if specified
    if (!antenna.empty()) {
        sdr_source.set_antenna(antenna);
    }
    
    // Create spectrum plot
    PlotCSpectrumBlock spectrum(
    "RF Spectrum", 
    {"Signal"}, 
    sample_rate, 
    2048  // FFT size
    );
    spectrum.set_initial_window(0.0f, 0.0f, 1200.0f, 600.0f);

    // Create flowgraph
    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&sdr_source, &spectrum.in[0]),
        cler::BlockRunner(&spectrum)
    );

    // Run flowgraph
    flowgraph.run();

    // GUI loop with frequency control
    float current_freq_mhz = freq_mhz;
    float current_gain = gain;

    while (!gui.should_close()) {
        gui.begin_frame();
        
        // Add controls
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("SDR Controls")) {
            ImGui::Text("Device: %s", device_args.c_str());
            ImGui::Text("Sample Rate: %.1f MSPS", sample_rate/1e6);
            ImGui::Separator();
            
            if (ImGui::SliderFloat("Frequency (MHz)", &current_freq_mhz, 24.0f, 1766.0f)) {
                sdr_source.set_frequency(current_freq_mhz * 1e6);
            }
            
            if (ImGui::SliderFloat("Gain (dB)", &current_gain, 0.0f, 50.0f)) {
                sdr_source.set_gain(current_gain);
            }
            
            ImGui::Separator();
            ImGui::Text("Common Frequencies:");
            if (ImGui::Button("FM Radio (100.3 MHz)")) {
                current_freq_mhz = 100.3f;
                sdr_source.set_frequency(current_freq_mhz * 1e6);
            }
            ImGui::SameLine();
            if (ImGui::Button("NOAA Weather (162.4 MHz)")) {
                current_freq_mhz = 162.4f;
                sdr_source.set_frequency(current_freq_mhz * 1e6);
            }
            if (ImGui::Button("ISM Band (433.92 MHz)")) {
                current_freq_mhz = 433.92f;
                sdr_source.set_frequency(current_freq_mhz * 1e6);
            }
            ImGui::SameLine();
            if (ImGui::Button("ADS-B (1090 MHz)")) {
                current_freq_mhz = 1090.0f;
                sdr_source.set_frequency(current_freq_mhz * 1e6);
            }
        }
        ImGui::End();
        
        // Render spectrum
        spectrum.render();
        
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 FPS
    }

    // Stop flowgraph
    flowgraph.stop();
    return 0;
}