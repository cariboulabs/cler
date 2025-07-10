# Cler: Compile Time DSP Flowgraph Framework for SDRs and Embedded Systems

Cler is a C++ header only template-based framework for constructing and executing flowgraphs of DSP processing blocks.
Its goal is to keep a tiny core while allowing maximal flexability:

* Code first, UI second.
* Defining blocks amounts to implementing a struct with a method.
* Channels are type agnostic.
* Flowgraphs are, and Blocks can be made completely static
* Tailored for Embedded Systems -  even MCUs
* Built for radio, but can also be used for control and dynamic simulations (supports cyclic graphs, and online modifiable params)
* Cross-Platform

**What's so special?** historically DSP flowgraph implementations rely on polymorphism to abstract over blocks and channels. This can constrain the architecture and lead to compromises — for example, GNURadio uses void* inputs/outputs in its procedure calls to achieve runtime flexibility. Instead, Cler uses variadic templates to achieve type safety and flexibility without runtime overhead. This approach was made possible by C++17 features like std::apply and forward deduction guide. The same approach can be taken in Rust with Traits and Zig with CompTime, but these languages still don't have the facilities for mature DSP on **embedded system**.

**How to use it?** Just Include `include/cler.hpp` and you are good for the basics. Want to use already written blocks? Inlcude them from `blocks/*`

Want to try out some examples?
```
mkdir build
cd build
cmake ..
make -j"$(nproc --ignore=1)"   # Use all cores-1
cd examples
./hello_world (or mass_spring_damper if you want to see somthing cool)
```

⚠️ Just one thing to look out for... because Cler is template heavy, error messages can be overwhelming. But no worries, with the small context window that is Cler, any LLM can help you out with ease. Eventuallty we will have a validator that can help debug common issues.

# Things to Know

* **Flowgraph vs Streamlined** </br>
Cler supports two architectural styles:
    * **flowgraph** </br>
    In flowgraph mode, you manually define each block and the channels that connect them. You then pass the connection structure into a flowgraph that is incharge of running the blocks. Behind the scenees, Cler flowgraph creates an OS thread for every block which constantly calls its `procedure()`. When the call returns an error, it yeilds to other threads before trying again.

    * **streamlined** </br>
    In streamlined mode, you are in charge of writing the loop, and you are in charge of passing samples from one block to the other.

    When the blocks are simple, the streamlined approach will be faster than the flowgraph becuase of the thread overhead. As a compromise, you can create `superblocks` which combine multiple small blocks.
    See `streamlined` and  `flowgraph` as examples for the two architectural styles, and `polyphase_channelizer` for a superblock implementation.

* **Blocks**: </br>
Blocks is a library of useful blocks for quick "plug and play". Its soft depedencies are `liquid`, `imdeargui` and `zf_log` brought in by CMAKE's fetch content. </br>
In CLER, it is rather easy to create blocks for specific use cases. As such, the library blocks were decided to be exactly the opposite - broad and general. There, we don't optimize minimal work sizes, and we dont template where we dont have to. Everything that can go on the heap - goes on the heap. These blocks should be GENERAL for quick mockup tests.

* **Blocks/GUI**: </br>
Cler is a header only library, but includes a gui library (dearimgui) that is compiled. To use it, include `gui_manager.hpp` and link your executable against `cler_gui`. See the `plots` or `mass_spring_damper` examples.

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
* <ins>Flowgraph validation:</ins><br/>
To keep the blocks/channels structure free of overbearing boilerplate validation logic, the best approach is to create an external tool that analyzes the application’s C++ code and validates it:
   - Do all blocks have runners?
   - Are all runners provided to the flowgraph?
   - etc..

    Additionally, we could develop a VS Code extension to automate these checks.

* <ins>Comparing to GnuRadio / FutureSDR:</ins> </br>
Its important that we know where we stand. We need to measure our performence against the best in the buissness and produce a report.

* <ins>Testing / CI / Profiling:</ins> </br>
If we are already producing a report, might aswell build a benchmark for core patterns to endure performence doesnt regress with updates

* <ins>Documentation:</ins> </br>
We need something that helps new developers start their journey. Doxygen / Sphinx via Breath? 

* <ins>GUI FrontEnd:</ins> </br>
While not a preference, if we are already creating a reflection tool for FlowGraph validation, we could also create an interactive FlowGraph generator. Could be some Desktop Application, that scans the /blocks folders, generates an interface markup file for each block, and then uses this information to allow the user to connect blocks on a canvas.
Importat:
    - Has to be cross platform.
    - Will not force blocks to implement markup files. Has to be generated from their .hpp code.

* <ins>Hardware Support:</ins> </br>
If we are serious about this, we need to support workflows that use the ubiquitous 2.4 GHz and 5.2 GHz bands. For this, we must ensure support for commodity hardware. So introducing source/sink blocks for these devices is welome.

* <ins>GPU Support:</ins> </br>
GPU can be instrumental on processing higher volumes. Creating ChannelGPU which uses the ChannelBase interface would allow users to write their GPU blocks.

# Contributing
- ✅ **Modern C++ (C++20)** — but always mindful of embedded constraints.
- ⚙️ **Keep the hardware interface size warning enabled** — so users understand what’s happening under the hood.
- ⚡ **Prefer templates and function pointers** — avoid `std::function` and use lambdas only if required.
- 🧩 **Avoid `std::any`** — to keep type safety explicit and predictable.
- 🔗 **Favor composition over inheritance** — except for simple interfaces.
- 🔒 **No try/catch for flow control** — use `cler::Result` for recoverable errors; `throw` only for unrecoverable states. `assert` is fine for startup guarantees.
- 🗒️ **Metadata inline** — no separate tag streams; encode what you need in the channel type or pass via callbacks.
- 🛠️ **Implementation guidelines** — Keep heavy implementations in `.cpp` files when possible (for example, when dealing with a single data type). Templated libraries already add compile-time cost, so we want to reduce that load whenever possible.
- ✅ **Meaningful pull requests** — improvements, bug fixes, and useful features are all welcome. Please bundle small changes together when possible.

# Acknowledgements
Special thanks to:
* Drogalis — for the excellent SPSC-Queue implementation that our buffers are based on.
* Bastian Bloessl and the FutureSDR community — Your design choices inspired some of ours.
* Joseph D. Gaeddert and the liquid-dsp community — In our opinion, the best DSP library out there by a margin.
* Omar Ocornut and The Dear ImGui community — A fast "batteries included" GUI library that meets all of our needs.
* The GNU Radio community — The benchmark to beat for open-source SDR frameworks.
