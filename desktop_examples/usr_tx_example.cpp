// usrp_tx_simple.cpp - Direct UHD transmission without Cler framework

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/types/tune_request.hpp>
#include <complex>
#include <iostream>
#include <fstream>
#include <vector>
#include <csignal>
#include <atomic>
#include <chrono>


std::atomic<bool> stop_signal(false);

void sig_int_handler(int) {
    stop_signal = true;
}

int main(int argc, char** argv) {
    // Parse command line
    if (argc < 5) {
        std::cout << "Usage: " << argv[0] << " <file> <freq_hz> <rate_hz> <gain_db> [device_args]" << std::endl;
        std::cout << "Example: " << argv[0] << " samples.bin 915e6 2e6 40" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    double freq = std::stod(argv[2]);
    double rate = std::stod(argv[3]);
    double gain = std::stod(argv[4]);
    std::string device_args = (argc > 5) ? argv[5] : "";

    // Setup signal handler
    std::signal(SIGINT, sig_int_handler);

    try {
        // Load samples from file
        std::cout << "Loading samples from: " << filename << std::endl;
        std::ifstream file(filename, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return 1;
        }

        size_t file_size = file.tellg();
        size_t num_samples = file_size / sizeof(std::complex<float>);
        
        std::vector<std::complex<float>> samples(num_samples);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(samples.data()), file_size);
        file.close();

        std::cout << "Loaded " << num_samples << " samples (" 
                  << num_samples / rate * 1000 << " ms)" << std::endl;

        // Create USRP
        std::cout << "\nCreating USRP device..." << std::endl;
        uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
        
        std::cout << "Using device: " << usrp->get_pp_string() << std::endl;

        // Set sample rate
        std::cout << "\nSetting TX rate: " << rate/1e6 << " MSPS" << std::endl;
        usrp->set_tx_rate(rate);
        std::cout << "Actual TX rate: " << usrp->get_tx_rate()/1e6 << " MSPS" << std::endl;

        // Set frequency
        std::cout << "Setting TX freq: " << freq/1e6 << " MHz" << std::endl;
        uhd::tune_request_t tune_request(freq);
        usrp->set_tx_freq(tune_request);
        std::cout << "Actual TX freq: " << usrp->get_tx_freq()/1e6 << " MHz" << std::endl;

        // Set gain
        std::cout << "Setting TX gain: " << gain << " dB" << std::endl;
        usrp->set_tx_gain(gain);
        std::cout << "Actual TX gain: " << usrp->get_tx_gain() << " dB" << std::endl;

        // Set thread priority
        uhd::set_thread_priority_safe();

        // Create TX stream
        std::cout << "\nCreating TX stream..." << std::endl;
        uhd::stream_args_t stream_args("fc32", "sc16");
        stream_args.channels = {0};
        uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);

        size_t samps_per_buff = tx_stream->get_max_num_samps();
        std::cout << "Max samples per buffer: " << samps_per_buff << std::endl;

        // Prepare metadata
        uhd::tx_metadata_t md;
        md.start_of_burst = false; //true                              
        md.end_of_burst = false;
        md.has_time_spec = false; //false

        std::cout << "\nTransmitting... Press Ctrl+C to stop" << std::endl;

        size_t sample_index = 0;
        size_t total_sent = 0;
        size_t underflow_count = 0;


        // auto start = std::chrono::high_resolution_clock::now();

        while (!stop_signal) {
            // Wrap around to loop the file
            if (sample_index >= num_samples) {
                sample_index = 0;
            }

            // Calculate how many samples to send
            size_t samples_to_send = std::min(samps_per_buff, num_samples - sample_index);
            auto start = std::chrono::high_resolution_clock::now();

            // Send samples
            size_t num_sent = tx_stream->send(
                &samples[sample_index],
                samples_to_send,
                md,
                0.1  // 100ms timeout
            );
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            std::cout << "\nElapsed time: " << elapsed.count()*1000 << "ms" << std::flush;
            // After first packet, clear start_of_burst
            md.start_of_burst = false;

            sample_index += num_sent;
            total_sent += num_sent;

            // Check for underflows (non-blocking)
            uhd::async_metadata_t async_md;
            if (tx_stream->recv_async_msg(async_md, 0.0)) {
                if (async_md.event_code == uhd::async_metadata_t::EVENT_CODE_UNDERFLOW ||
                    async_md.event_code == uhd::async_metadata_t::EVENT_CODE_UNDERFLOW_IN_PACKET) {
                    underflow_count++;
                    std::cout << "U" << std::flush;
                }
            }

            // Print progress every second
            if (total_sent % (size_t)rate == 0) {
                std::cout << "\rSent: " << total_sent / rate << "s, Underflows: " 
                         << underflow_count << "     " << std::flush;
            }
        }

        // Send end of burst
        std::cout << "\n\nSending end of burst..." << std::endl;
        md.end_of_burst = false; //true
        tx_stream->send("", 0, md);

        std::cout << "Total samples sent: " << total_sent << std::endl;
        std::cout << "Total underflows: " << underflow_count << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Done." << std::endl;
    return 0;
}