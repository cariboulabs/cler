#include "cler.hpp"
#include "desktop_blocks/blob.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include "desktop_blocks/udp/sink_udp.hpp"
#include "desktop_blocks/udp/source_udp.hpp"
#include "desktop_blocks/sinks/sink_null.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <array>

const size_t MAX_UDP_BLOB_SIZE = 256;
const size_t SLAB_SLOTS = 10;
const size_t FIXED_ARRAY_SIZE = 256;

// =============================================================================================
// Source side: example of using a slab and sending over Blobs (essentially pointers to the slab)
//              This can be used for variable-size data like LoRa packets with different lengths
// ==============================================================================================
struct SourceBlobBlock : public cler::BlockBase {
    Slab _slab{SLAB_SLOTS, MAX_UDP_BLOB_SIZE};

    SourceBlobBlock(const char* name)
        : cler::BlockBase(name) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Blob>* out) {
        if (out->space() == 0) {
            return cler::Error::NotEnoughSpace;
        }

        auto result = _slab.take_slot();
        if (result.is_err()) {
            return result.unwrap_err();
        }

        Blob blob = result.unwrap();
        char msg[256];
        snprintf(msg, sizeof(msg), "Hello udp! #%zu", counter++);

        size_t msg_len = strlen(msg);
        if (msg_len + 1 > blob.len) {  // +1 for null terminator
            blob.release();
            return cler::Error::BadData;
        }

        memcpy(blob.data, msg, msg_len);
        blob.data[msg_len] = '\0';  // Null terminate
        blob.len = msg_len + 1;     // Include null terminator in length

        out->push(blob);
        return cler::Empty{};
    }

private:
    size_t counter = 0;
};

// ============================================================================
// Sink side: Fixed-size array from UDP
// ============================================================================

void print_received_array(const std::array<uint8_t, FIXED_ARRAY_SIZE>& arr, void* context) {
    // arr.data() is now null-terminated, so use C-string constructor
    std::string msg(reinterpret_cast<const char*>(arr.data()));
    std::cout << "Received array UDP message: " << msg << std::endl;
}

int main() {
    std::cout << "=== UDP Example: Blob → UDP → std::array ===" << std::endl;
    std::cout << "Source generates variable-size Blobs" << std::endl;
    std::cout << "Sends over UDP on 127.0.0.1:9001" << std::endl;
    std::cout << "Sink receives as fixed-size std::array" << std::endl;
    std::cout << std::endl;

    // Source: generate Blobs
    SourceBlobBlock source_blob("SourceBlob");

    // Sink Blob to UDP: send Blobs over network
    SinkUDPSocketBlock<Blob> sink_blob_udp("SinkBlobUDP",
                                            UDPBlock::SocketType::INET_UDP,
                                            "127.0.0.1:9001");

    // Source UDP to Array: receive from network as fixed-size arrays
    SourceUDPSocketBlock<std::array<uint8_t, FIXED_ARRAY_SIZE>> source_array_udp(
                                "SourceArrayUDP",
                                UDPBlock::SocketType::INET_UDP,
                                "127.0.0.1:9001",
                                nullptr,
                                print_received_array
                                );

    SinkNullBlock<std::array<uint8_t, FIXED_ARRAY_SIZE>> sink_null("SinkNull");

    // Create flowgraph
    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source_blob, &sink_blob_udp.in),
        cler::BlockRunner(&sink_blob_udp),
        cler::BlockRunner(&source_array_udp, &sink_null.in),
        cler::BlockRunner(&sink_null)
    );

    fg.run();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
