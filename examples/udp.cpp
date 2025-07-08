#include "cler.hpp"
#include "blocks/udp/sink_udp.hpp"
#include "blocks/udp/source_udp.hpp"
#include "blocks/sink_terminal.hpp"
#include <queue>
#include <iostream>
#include <thread>
#include <chrono>

constexpr const size_t MAX_UDP_BLOB_SIZE = 256;

struct SourceDatagramBlock : public cler::BlockBase {
    UDPBlock::Slab _slab {100, MAX_UDP_BLOB_SIZE}; // 100 slots, each 256 bytes

    SourceDatagramBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<UDPBlock::BlobSlice>* out) {
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        auto result = _slab.take_slot();
        if (result.is_err()) {
            return result.unwrap_err();
        }

        UDPBlock::BlobSlice slice = result.unwrap();
        char msg[256];
        snprintf(msg, sizeof(msg), "Hello, UDP! #%zu", counter++);

        //always be cautious
        size_t msg_len = strlen(msg);
        if (msg_len > slice.len) {
            _slab.release_slot(slice.slot_idx);
            return cler::Error::ProcedureError;
        }

        memcpy(slice.data, msg, msg_len);
        slice.len = msg_len;

        out->push(slice);

        return cler::Empty{};
    }

private:
    size_t counter = 0;
};

void on_udp_source_receive(const UDPBlock::BlobSlice& slice, [[maybe_unused]] void* context) {
    std::cout << "Received UDP data: " << std::string(reinterpret_cast<char*>(slice.data), slice.len) << std::endl;
}

size_t on_sink_terminal_receive(cler::Channel<UDPBlock::BlobSlice>& channel, [[maybe_unused]] void* context) {
    UDPBlock::Slab* slab = static_cast<UDPBlock::Slab*>(context);
    UDPBlock::BlobSlice slice;
    size_t work_size = channel.size();
    for (size_t i = 0; i < work_size; ++i) {
        channel.pop(slice);
        slab->release_slot(slice.slot_idx);
    }
    return work_size;
}

int main() {
    SourceDatagramBlock source_datagram("SourceDatagram");
    SinkUDPSocketBlock sink_udp("SinkUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1", 9001, source_datagram._slab.get_free_slots_q());
    SourceUDPSocketBlock source_udp("SourceUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1", 9001,
                      MAX_UDP_BLOB_SIZE, 100, on_udp_source_receive, nullptr);
    SinkTerminalBlock<UDPBlock::BlobSlice> sink_terminal("SinkTerminal", on_sink_terminal_receive, &source_datagram._slab);

    cler::BlockRunner source_datagram_runner(&source_datagram, &sink_udp.in);
    cler::BlockRunner sink_udp_runner(&sink_udp);
    cler::BlockRunner sink_terminal_runner(&sink_terminal);
    cler::BlockRunner source_udp_runner(&source_udp, &sink_terminal.in);


    cler::FlowGraph fg(
                    source_datagram_runner,
                    sink_udp_runner,
                    source_udp_runner,
                    sink_terminal_runner
                    );

    fg.run();

    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
