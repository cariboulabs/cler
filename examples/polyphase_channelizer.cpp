#include "cler.hpp"
#include "blocks/polyphase_channelizer.hpp"

int main() {
    PolyphaseChannelizer channelizer("Channelizer", 4, 1024, 256);
    return 0;
}