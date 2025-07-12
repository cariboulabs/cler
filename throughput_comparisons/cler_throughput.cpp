#include "cler.hpp"
#include "blocks/sink_terminal.hpp"
#include "blocks/throughput.hpp"
#include "cler_addons.hpp"

const size_t BUFFER_SIZE = 2;

struct SourceBlock : public cler::BlockBase {
    SourceBlock(std::string name)  : BlockBase(std::move(name)) {} 

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        size_t out_space = out->space();
        if (out_space < BUFFER_SIZE) {
            return cler::Error::NotEnoughSpace;
        }

        out->writeN(_tmp, BUFFER_SIZE);
        return cler::Empty{};
    }

    private:
    std::complex<float> _tmp[BUFFER_SIZE] = {0.0f, 0.0f}; // Example data
};

struct TransferBlock : public cler::BlockBase {
    cler::Channel<std::complex<float>> in;

    TransferBlock(std::string name)  : BlockBase(std::move(name)), in(BUFFER_SIZE) {} 

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<std::complex<float>>* out) {
        size_t in_size = in.size();
        size_t out_space = out->space();
        if (in_size == 0) {
            return cler::Error::NotEnoughSamples;
        }
        if (out_space < in_size) {
            return cler::Error::NotEnoughSpace;
        }
        size_t transferable = std::min(in_size, out_space);

        in.readN(_tmp, transferable);
        out->writeN(_tmp, transferable);
        return cler::Empty{};
    }

    private:
    std::complex<float> _tmp[BUFFER_SIZE];

};

int main() {

    SourceBlock source("Source");
    TransferBlock transfer1("Transfer1");
    TransferBlock transfer2("Transfer2");
    TransferBlock transfer3("Transfer3");
    TransferBlock transfer4("Transfer4");
    ThroughputBlock<std::complex<float>> throughput("Throughput1", BUFFER_SIZE);
    SinkTerminalBlock<std::complex<float>> sink("Sink", nullptr, nullptr, BUFFER_SIZE);

    cler::BlockRunner source_runner(&source, &transfer1.in);
    cler::BlockRunner transfer1_runner(&transfer1, &transfer2.in);
    cler::BlockRunner transfer2_runner(&transfer2, &transfer3.in);
    cler::BlockRunner transfer3_runner(&transfer3, &transfer4.in);
    cler::BlockRunner transfer4_runner(&transfer4, &throughput.in);
    cler::BlockRunner throughput_runner(&throughput, &sink.in);
    cler::BlockRunner sink_runner(&sink);

    cler::FlowGraph flowgraph(
        source_runner,
        transfer1_runner,
        transfer2_runner,
        transfer3_runner,
        transfer4_runner,
        throughput_runner,
        sink_runner
    );
    
    
    printf("Running CLER throughput comparison example. Please wait 10 seconds...\n");
    flowgraph.run();
    for (size_t i = 0; i < 10; ++i) {
        // Simulate continuous operation
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    flowgraph.stop();
    throughput.report();

    return 0;
}