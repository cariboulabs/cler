#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <cstring> // for memcpy

const size_t CHANNEL_SIZE = 512;

struct SourceOneBlock : public cler::BlockBase {
    SourceOneBlock(const char* name)  : BlockBase(name) {
        for (size_t i = 0; i < CHANNEL_SIZE; ++i) {
            _ones[i] = 1.0;
        }
    } 

    cler::Result<cler::Empty, cler::Error> procedure(
        cler::ChannelBase<float>* out) {

        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        size_t to_write = std::min(write_size, CHANNEL_SIZE);
        if (to_write > 0) {
            std::memcpy(write_ptr, _ones, to_write * sizeof(float));
            out->commit_write(to_write);
        }
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

        // Use zero-copy path
        auto [write_ptr, write_size] = out->write_dbf();
        size_t to_write = std::min(write_size, CHANNEL_SIZE);
        if (to_write > 0) {
            std::memcpy(write_ptr, _twos, to_write * sizeof(float));
            out->commit_write(to_write);
        }
        return cler::Empty{};
    }

    private:
        float _twos[CHANNEL_SIZE];
};

struct Gain2Block : public cler::BlockBase {
    cler::Channel<float> in; // Heap allocated for dbf support

    Gain2Block(const char* name) : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t to_process = std::min(read_size, write_size);
        if (to_process > 0) {
            for (size_t i = 0; i < to_process; ++i) {
                write_ptr[i] = read_ptr[i] * 2.0f;
            }
            in.commit_read(to_process);
            out->commit_write(to_process);
        }
        return cler::Empty{};
    }
};

struct Gain3Block : public cler::BlockBase {
    cler::Channel<float> in; // Heap allocated for dbf support

    Gain3Block(const char* name) : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        // Use zero-copy path
        auto [read_ptr, read_size] = in.read_dbf();
        auto [write_ptr, write_size] = out->write_dbf();
        
        size_t to_process = std::min(read_size, write_size);
        if (to_process > 0) {
            for (size_t i = 0; i < to_process; ++i) {
                write_ptr[i] = read_ptr[i] * 3.0f;
            }
            in.commit_read(to_process);
            out->commit_write(to_process);
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

    SinkPrintBlock(const char* name) : BlockBase(name), in(cler::DOUBLY_MAPPED_MIN_SIZE / sizeof(float)) {}

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