<div align="center">
  <img src="misc/logo.jpeg" alt="CLER Logo" width="600"/>
  <h1>a Compile Time DSP Flowgraph for SDRs and Embedded Systems</h1>
  <p><a href="https://cariboulabs.github.io/cler/" target="_blank"><strong>Open the Onboarding Page</strong></a></p>
</div>

Cler is a C++ template-based framework for constructing and executing flowgraphs of DSP processing blocks.
Its goal is to keep a tiny header only core allowing maximal flexibility:

* Defining blocks amounts to implementing a struct with a method
* Channels are type agnostic
* Blocks are not limited by number of input/output Channels
* Callbacks can be used for communication between unconnected blocks
* Flowgraphs and Blocks can be made almost completely static
* Tailored for Embedded Systems -  even MCUs
* Built for radio, but can also be used for control and dynamic simulations (supports cyclic graphs, and online modifiable params)
* Cross-Platform
* Code first, Flowgraph GUI second. While No-code is sweet, it also constrains applications

**But embedded devices dont need DSP do they?**
Embedded Linux aside, most embedded devices traditionally relied on dedicated chips — for fusion, filtering, or modulation. But with today’s powerful SoCs and the rise of agentic AI, it’s often faster, cheaper, and more flexible to move DSP into software. Cler aims to fill that gap.

**Why reinvent the DSP wheel?** Existing frameworks rely heavily on runtime polymorphism to manage blocks and channels. This adds overhead, limits type safety, and complicates deployment on resource-constrained systems. For example, GNU Radio uses void* buffers in its work() calls to achieve flexibility, but that sacrifices clarity and static guarantees. Cler takes a different path: using C++17 features like variadic templates and std::apply, it achieves compile-time safety, zero-cost abstraction, and minimal runtime footprint — making it practical for everything from desktop SDR to bare-metal MCUs.

**How does it compare to GNURadio or FutureSDR**? It’s not trying to — though it can be competitive in desktop environments. Cler isn’t a general-purpose SDR toolkit; it’s built from the ground up for embedded systems, where memory is limited, timing is critical, and you can’t rely on having an MMU. Instead of shared runtime schedulers or double-mapped buffers, Cler uses block-owned ring buffers and minimal coordination to keep things simple and deterministic. Yet it remains powerful enough for many SDR and control applications — just without the overhead.

Want to try out some examples on a Desktop?
```
mkdir build
cd build
cmake ..
make -j"$(nproc --ignore=1)"   # Use all cores-1
cd desktop_examples
./hello_world #(or mass_spring_damper if you want to see something cool)
```

⚠️ Just one thing to watch for: Cler’s template-heavy design can produce overwhelming errors, but any LLM can help with the small context window that is Cler. 

# Okay, but how does it write?

Below is `desktop_examples/hello_world.cpp`

```
int main() {
    cler::GuiManager gui(800, 400, "Hello World Plot Example");

    const size_t SPS = 1000;
    SourceCWBlock<float> source1("CWSource", 1.0f, 1.0f, SPS); //amplitude, frequency
    SourceCWBlock<float> source2("CWSource2", 1.0f, 20.0f, SPS);
    ThrottleBlock<float> throttle("Throttle", SPS);
    AddBlock<float> adder("Adder", 2); // 2 inputs

    PlotTimeSeriesBlock plot(
        "Hello World Plot",
        {"Added Sources"},
        SPS,
        3.0f // duration in seconds
    );
    plot.set_initial_window(0.0f, 0.0f, 800.0f, 400.0f); //x,y, width, height

    auto flowgraph = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source1, &adder.in[0]),
        cler::BlockRunner(&source2, &adder.in[1]),
        cler::BlockRunner(&adder, &throttle.in),
        cler::BlockRunner(&throttle, &plot.in[0]),
        cler::BlockRunner(&plot)
    );

    flowgraph.run();

    while (gui.should_close() == false) {
        gui.begin_frame();
        plot.render();
        gui.end_frame();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}
```

# Things to Know

* **Schedulers** </br>
Cler includes two schedulers: **ThreadPerBlock** (default, simple, debuggable) and **FixedThreadPool** (better for constrained systems).  It also has Performance mode which eliminates stats overhead for ultra-high throughput applications,
and Adaptive_sleep mode which can help mitigate chocked CPU in expensive of throughput.

* **Flowgraph vs Streamlined** </br>
Cler supports two architectural styles:
    * **flowgraph** </br>
    In flowgraph mode, you manually define each block and the channels that connect them. You then pass the connection structure into a flowgraph that is incharge of running the blocks. Behind the scenees, Cler flowgraph creates an OS thread for every block which constantly calls its `procedure()`. When the call returns an error, it yeilds to other threads before trying again.

    * **streamlined** </br>
    In streamlined mode, you are in charge of writing the loop, and you are in charge of passing samples from one block to the other.

    When the blocks are simple, the streamlined approach will be faster than the flowgraph becuase of the thread overhead. As a compromise, you can create `superblocks` which combine multiple small blocks.
    See `streamlined` and  `flowgraph` as examples for the two architectural styles, and `polyphase_channelizer` for a superblock implementation.

* **Desktop Blocks**: </br>
`desktop_blocks` is a library of useful blocks for quick "plug and play". Its soft depedencies are `liquid`, `imdeargui`(with opengl,glfw).
To include its headers and link against it, link against `cler::cler_blocks` with CMake.
In Cler, it is rather easy to create blocks for specific use cases. As such, the library blocks were decided to be exactly the opposite - broad and general. There, we don't optimize minimal work sizes, and we dont template where we dont have to. Everything that can go on the heap - goes on the heap. These blocks should be GENERAL for quick mockup tests.

* **Buffers** </br>
Our buffers are modified version of `https://github.com/drogalis/SPSC-Queue`. They allow for static or heap allocation. See the gain block in `streamlined.cpp` for an example.</br> 
Cler supports three buffer access patterns: 
    * **Push/Pop** </br>
    For single values. there is also a try push/pop you can use if you dont inspect size() beforehand.
    Remember though, after you have poped a value, you must not put it back! Cler channels are lock-free SPSC that *ASSUME* that one thread is a writer while another is a reader. No mixin' it up. </br>
    This is **SLOW** in our context. Always prefer the other access patterns.

    * **Peek/Commit** </br>
    Allows you to inspect (peek) data in the buffer without removing it, then explicitly commit the number of items you’ve processed.
    The downside is that you can only access data up to the physical end of the ring buffer at a time — so if your logical window wraps, you may need to handle two chunks.

    * **Read/Write (your go-to)**. </br>
    Provides access to the full available buffer space for larger chunks of data. You’ll typically copy data to a temporary buffer for processing. Read/Write automatically advances the ring buffer pointers for you — no manual commit needed. This should be your *go to* pattern.

# RoadMap
Below is a wish-list for this library, sorted by importance.

* <ins>Comparing to GnuRadio / FutureSDR:</ins> </br>
Its important that we know where we stand. We need to measure our performence against the best in the buissness and produce a report.

* <ins>Testing / CI / Profiling:</ins> </br>
If we are already producing a report, might aswell build a benchmark for core patterns to endure performence doesnt regress with updates

* <ins>Blocks for Embedded Systems:</ins>
Our /Blocks library is built as a broad, general-purpose toolkit for desktop experiments and quick testing — but for real end-node applications, we need an /EmbeddedBlocks.

* <ins>Hardware Support:</ins> </br>
If we are serious about this, we need to support real hardware: FPGAs, SDRs, DAC/ADC boards, RF transceivers, and similar peripherals. For this, we must ensure support for commodity hardware by introducing source/sink blocks for them.

* <ins>Flowgraph validation:</ins><br/>
We need to address the current situation where small mistakes can lead to pages of confusing compiler errors. While it’s possible to add validation logic directly into the blocks and flowgraph, this would introduce unnecessary boilerplate and clutter. A better approach is to create an external tool that analyzes the application’s C++ code and validates it:

   - Do all blocks have runners?
   - Are all runners provided to the flowgraph?
   - Are there any missing or misconfigured connections?
   - ...

    Additionally, we could develop a VS Code extension to automate watch files and squiggle errors.

* <ins>GUI FrontEnd:</ins> </br>
This is more of a nice to have, but if we are already creating a reflection tool for Flowgraph validation, we could also create an interactive FlowGraph generator.
Could be some Desktop Application, that scans the /blocks folders, generates an interface markup file for each block, and then uses this information to allow the user to connect blocks on a canvas.
Importat:
    - Has to be cross platform.
    - Will not force blocks to implement markup files. Has to be generated from their .hpp code.

# Contributing
We welcome any contribution — and constructive criticism too!  
There’s a lot we don’t know yet and plenty to learn from the SDR community.

How to add code:
- ✅ **Modern C++ (C++17)** — but always mindful of embedded constraints.
- 🌲 **Improve existing blocks** — solo-developing also means one pair of eyes. Fresh looks are always welcome.
- ⚡ **Prefer templates and function pointers** — avoid `std::function` and use lambdas only if required.
- 🧩 **Avoid `std::any`** — to keep type safety explicit and predictable.
- 🔗 **Favor composition over inheritance** — except for simple interfaces.
- 🔒 **No try/catch for flow control** — use `cler::Result` for recoverable errors; `throw` only for unrecoverable states. `assert` is fine for startup guarantees.
- 🗒️ **Metadata inline** — no separate tag streams; encode what you need in the channel type or pass via callbacks.
- 🛠️ **Implementation guidelines** — Keep heavy implementations in `.cpp` files when possible (for example, when dealing with a single data type). Templated libraries already add compile-time cost, so we want to reduce that load wherever possible.

# Acknowledgements
Special thanks to:
* Andrew Drogalis — for the excellent SPSC-Queue implementation that used as our channels.
* Bastian Bloessl and the FutureSDR community — Your design choices inspired some of ours.
* Joseph D. Gaeddert and the liquid-dsp community — In our opinion, the best DSP library out there by a margin.
* Omar Ocornut and The Dear ImGui community — A fast "batteries included" GUI library that meets all of our needs.
* The GNU Radio community — The benchmark to beat for open-source SDR frameworks.
