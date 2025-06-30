#include "cler.hpp"
#include "blocks/polyphase_channelizer.hpp"

int main() {
    PolyphaseChannelizer channelizer("Channelizer", 4, 80.0f, 512, 256);
    return 0;
}