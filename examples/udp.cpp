#include "cler.hpp"
#include "blocks/udp/sink_udp.hpp"
#include "blocks/udp/source_udp.hpp"
#include <queue>
#include <iostream>
#include <thread>
#include <chrono>

struct SourceDatagramBlock : public cler::BlockBase {
    SourceDatagramBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<UDPBlock::BlobSlice> out) {
        if (out.space() == 0) {
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

        out.push(slice);

        return cler::Empty{};
    }

private:
    size_t counter = 0;
    UDPBlock::Slab _slab {100, 256}; // 100 slots, each 256 bytes
};


int main() {
    constexpr size_t BLOB_SIZE = 1024;
    constexpr size_t NUM_SLOTS = 8;

    uint8_t slab[NUM_SLOTS * BLOB_SIZE];
    std::queue<size_t> free_slots;
    for (size_t i = 0; i < NUM_SLOTS; ++i) {
        free_slots.push(i);
    }

    SourceDatagramBlock source_datagram("SourceDatagram");
    SinkUDPSocketBlock sink_udp("SinkUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1", 9001, free_slots);
    SourceUDPSocketBlock source_udp("SourceUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1", 9001, slab, BLOB_SIZE, free_slots);

    // Flow: SourceDatagram -> SinkUDPSocket -> UDP network -> SourceUDPSocket
    source_datagram.out.connect(&sink_udp.in);
    source_datagram.out.connect(&sink_udp.in);
    source_udp.out.connect(nullptr); // Not used — just callback

    cler::BlockRunner source_runner(&source_datagram);
    cler::BlockRunner sink_runner(&sink_udp);
    cler::BlockRunner udp_source_runner(&source_udp);

    cler::FlowGraph graph(source_runner, sink_runner, udp_source_runner);

    graph.run();

    // Run for a bit
    for (size_t i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    graph.stop();
    return 0;
}
