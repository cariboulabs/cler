#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>

const size_t CHANNEL_SIZE = 512;

struct SourceOneBlock : public cler::BlockBase {
    SourceOneBlock(const char* name)  : BlockBase(name) {
        for (size_t i = 0; i < CHANNEL_SIZE; ++i) {
            _ones[i] = 1.0;
        }
    } 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out) {

        out->writeN(_ones, out->space());
        return cler::Empty{};
    }

    private:
        float _ones[CHANNEL_SIZE];
};

struct SourceTwoBlock : public cler::BlockBase {
    SourceTwoBlock(const char* name)  : BlockBase(name) {
        for (size_t i = 0; i < CHANNEL_SIZE; ++i) {
            _twos[i] = 2.0f;
        }
    } 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out) {

        out->writeN(_twos, out->space());
        return cler::Empty{};
    }

    private:
        float _twos[CHANNEL_SIZE];
};

struct Gain2Block : public cler::BlockBase {
    cler::Channel<float, CHANNEL_SIZE> in; //this is a stack buffer!

    Gain2Block(const char* name) : BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t transferable = std::min(in.size(), out->space());
        for (size_t i = 0; i < transferable; ++i) {
            float value;
            in.pop(value);
            out->push(value * 2.0f);
        }
        return cler::Empty{};
    }
};

struct Gain3Block : public cler::BlockBase {
    cler::Channel<float, CHANNEL_SIZE> in; //this is a stack buffer!

    Gain3Block(const char* name) : BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        size_t transferable = std::min(in.size(), out->space());
        for (size_t i = 0; i < transferable; ++i) {
            float value;
            in.pop(value);
            out->push(value * 3.0f);
        }
        return cler::Empty{};
    }
};

struct switchSourceBlock : public cler::BlockBase {
    std::variant<SourceOneBlock, SourceTwoBlock> source;
    
    switchSourceBlock(const char* name, uint8_t source_choice)
          : BlockBase(name),
            source(source_choice == 1 ?
                   std::variant<SourceOneBlock, SourceTwoBlock>(std::in_place_type<SourceOneBlock>, "SourceOne") :
                   std::variant<SourceOneBlock, SourceTwoBlock>(std::in_place_type<SourceTwoBlock>, "SourceTwo")) {
          if (source_choice < 1 || source_choice > 2) {
              throw std::invalid_argument("Invalid source choice. Must be 1 or 2.");
          }
      }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        return std::visit([&](auto& src) {
            return src.procedure(out);
        }, source);
    }
};

struct switchGainBlock : public cler::BlockBase {
    std::variant<Gain2Block, Gain3Block> gain;

    switchGainBlock(const char* name, uint8_t gain_choice)
          : BlockBase(name),
            gain(gain_choice == 2 ?
                  std::variant<Gain2Block, Gain3Block>(std::in_place_type<Gain2Block>, "Gain2") :
                  std::variant<Gain2Block, Gain3Block>(std::in_place_type<Gain3Block>, "Gain3")) {
          if (gain_choice < 2 || gain_choice > 3) {
              throw std::invalid_argument("Invalid gain choice. Must be 2 or 3.");
          }
      }

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        return std::visit([&](auto& g) {
            return g.procedure(out);
        }, gain);
    }

    cler::ChannelBase<float>* in() {
        return std::visit([](auto& g) -> cler::ChannelBase<float>* {
            return &g.in;
        }, gain);
    }
    
};

struct SinkPrintBlock : public cler::BlockBase {
    cler::Channel<float> in;

    SinkPrintBlock(const char* name) : BlockBase(name), in(CHANNEL_SIZE) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t transferable = in.size();
        for (size_t i = 0; i < transferable; ++i) {
            float value;
            in.pop(value);
            std::cout << "Received: " << value << std::endl;
        }
        return cler::Empty{};
    }
};

int main(int argc, char* argv[]) {
    // Show help if no args, too many args, or --help/-h is passed
    if (argc == 1 || argc > 3 || 
        (argc >= 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h"))) {
        
        std::cout << "Usage: " << argv[0] << " <source_choice> <gain_choice>\n";
        std::cout << "  <source_choice>: 1 = SourceOne, 2 = SourceTwo\n";
        std::cout << "  <gain_choice>:   2 = Gain2, 3 = Gain3\n";
        std::cout << "\nExample:\n";
        std::cout << "  " << argv[0] << " 1 2   # Use SourceOne with Gain2\n";
        return 0;
    }

    if (argc == 2) {
        std::cerr << "Error: You must specify both source and gain choices.\n";
        return 1;
    }

    int source_choice = std::stoi(argv[1]);
    int gain_choice = std::stoi(argv[2]);
    if (source_choice < 1 || source_choice > 2 || gain_choice < 2 || gain_choice > 3) {
        std::cerr << "Invalid choices. Source choice must be 1 or 2, gain choice must be 2 or 3.\n";
        return 1;
    }

    switchSourceBlock source("SwitchSource", static_cast<uint8_t>(source_choice));
    switchGainBlock gain("SwitchGain", static_cast<uint8_t>(gain_choice));
    SinkPrintBlock sink("SinkPrint");

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, gain.in()),
        cler::BlockRunner(&gain, &sink.in),
        cler::BlockRunner(&sink)
    );

    flowgraph.run();


    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}