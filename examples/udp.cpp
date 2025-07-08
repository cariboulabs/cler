#include "cler.hpp"
#include "blocks/udp/sink_udp.hpp"
#include "blocks/udp/source_udp.hpp"
#include <queue>
#include <iostream>
#include <thread>
#include <chrono>

struct SourceDatagramBlock : public cler::BlockBase {
    SourceDatagramBlock(const char* name, uint8_t* slab, size_t blob_size, std::queue<size_t>& free_slots)
        : cler::BlockBase(name), _slab(slab), _blob_size(blob_size), _free_slots(free_slots) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<UDPBlock::BlobSlice>* out) {
        if (_free_slots.empty()) {
            return cler::Error::NotEnoughSpace;
        }
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        const char* msg = "hello udp\n";
        size_t msg_len = strlen(msg);

        size_t slot_idx = _free_slots.front();
        _free_slots.pop();
        uint8_t* ptr = _slab + (slot_idx * _blob_size);

        memcpy(ptr, msg, msg_len);

        UDPBlock::BlobSlice slice { ptr, msg_len, slot_idx };
        out->push(slice);

        return cler::Empty{};
    }

private:
    uint8_t* _slab;
    size_t _blob_size;
    std::queue<size_t>& _free_slots;
};

int main() {
    constexpr size_t BLOB_SIZE = 1024;
    constexpr size_t NUM_SLOTS = 8;

    uint8_t slab[NUM_SLOTS * BLOB_SIZE];
    std::queue<size_t> free_slots;
    for (size_t i = 0; i < NUM_SLOTS; ++i) {
        free_slots.push(i);
    }

    SourceDatagramBlock source_datagram("SourceDatagram", slab, BLOB_SIZE, free_slots);
    UDPBlock::SinkUDPSocketBlock sink_udp("SinkUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1", 9001, free_slots);
    UDPBlock::SourceUDPSocketBlock source_udp("SourceUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1", 9001, slab, BLOB_SIZE, free_slots);

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
