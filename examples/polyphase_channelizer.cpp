#include "cler.hpp"
#include "gui_manager.hpp"
#include "blocks/polyphase_channelizer.hpp"
#include "blocks/add.hpp"
#include "blocks/cw_source.hpp"

int main() {
    CWSourceBlock cw_source1("CW Source", 1, 100, 512);
    CWSourceBlock cw_source2("CW Source", 2, 100, 512);
    AddBlock<float> adder("Adder", 2, 512, 256);
    // PolyphaseChannelizerBlock channelizer("Channelizer", 4, 80.0f, 3, 512, 256);

    cler::BlockRunner cw_source1_runner{&cw_source1, &adder.in[0]};
    cler::BlockRunner cw_source2_runner{&cw_source1, &adder.in[1]};

    cler::GuiManager gui(1000, 400 , "Polyphase Channelizer Example");

    cler::FlowGraph flowgraph(
        cw_source1,
        cw_source2
    );

    flowgraph.run();

    while (!gui.should_close()) {
        gui.begin_frame();
        // freqplot.render();
        gui.end_frame();

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return 0;
}