#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/udp/sink_udp.hpp"
#include "desktop_blocks/udp/source_udp.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include <queue>
#include <iostream>
#include <thread>
#include <chrono>

/*
NOTE: SourceDatagramBlock is an ANTI-PATTERN in CLER!
Usually we would have the same block that generates data and send it over UDP.
Also, the the same block that receives datagrams can instiantiate data from the blobs before sending them to the next block.
No reason to burden computer with unnecessary work
still, it is here showcase capabilities

You can, and should use GenericDatagramSocket directly in your blocks.
 */

const size_t MAX_UDP_BLOB_SIZE = 100;
const size_t SLAB_SLOTS = 10; // Number of slots in the slab

struct SourceDatagramBlock : public cler::BlockBase {
    UDPBlock::Slab _slab {SLAB_SLOTS, MAX_UDP_BLOB_SIZE};

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
            return cler::Error::BadData;
        }

        memcpy(slice.data, msg, msg_len);
        slice.len = msg_len;

        out->push(slice);

        return cler::Empty{};
    }
private:
    size_t counter = 0;
};

void on_sink_udp_send(const UDPBlock::BlobSlice& slice, [[maybe_unused]] void* context) {
    assert(slice.data != nullptr);
    assert(slice.len > 0);
}

void on_source_udp_recv(const UDPBlock::BlobSlice& slice, [[maybe_unused]] void* context) {
    assert(slice.data != nullptr);
    assert(slice.len > 0);
}

size_t on_sink_null_recv(cler::Channel<UDPBlock::BlobSlice>* channel, [[maybe_unused]] void* context) {
    UDPBlock::BlobSlice slice;
    size_t work_size = channel->size();
    for (size_t i = 0; i < work_size; ++i) {
        channel->pop(slice);
        std::cout << "Received: " << std::string(reinterpret_cast<char*>(slice.data), slice.len) << std::endl;
        slice.release();
    }
    return 0; //we are doing the popping!
}

int main() {
    SourceDatagramBlock source_datagram("SourceDatagram");
    SinkUDPSocketBlock sink_udp("SinkUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1:9001", on_sink_udp_send);
    SourceUDPSocketBlock source_udp("SourceUDPSocket", UDPBlock::SocketType::INET_UDP, "127.0.0.1:9001",
                      MAX_UDP_BLOB_SIZE, SLAB_SLOTS, on_source_udp_recv, nullptr);
    SinkNullBlock<UDPBlock::BlobSlice> sink_null("SinkNull", on_sink_null_recv, nullptr, 20);

    auto fg = cler::make_desktop_flowgraph(
                    cler::BlockRunner(&source_datagram, &sink_udp.in),
                    cler::BlockRunner(&sink_udp),
                    cler::BlockRunner(&source_udp, &sink_null.in),
                    cler::BlockRunner(&sink_null)
                    );

    fg.run();

    while (true) {
        // Simulate some work in the main thread
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
