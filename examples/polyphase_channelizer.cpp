#include "cler.hpp"
#include "blocks/polyphase_channelizer.hpp"
#include "blocks/add.hpp"

int main() {
    AddBlock<float> addBlock("Adder", 2, 512, 256);
    PolyphaseChannelizerBlock channelizer("Channelizer", 4, 80.0f, 3, 512, 256);
    return 0;
}