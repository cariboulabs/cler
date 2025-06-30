# Cler: Compile-Time DSP Flowgraph Framework

cler is a C++ heaer only template-based framework for constructing and executing flowgraphs of processing blocks, especially for DSP, SDR, or other high-throughput streaming pipelines.
Its goal is to be tiny, and allow maximal flexability:
* All code, no frontend.
* Defining blocks amounts to implementing a struct with a method.
* Channels are type agnostic.

How to use it? Just Include `cler.hpp`

# Things to Know

* **Buffers** </br>
Our buffers are modified version of `https://github.com/drogalis/SPSC-Queue`. They allow for static or heap allocation. See  the gain block in `streamlined.cpp` for an example.

* **GUI**: </br>
Cler is a header only library, but includes a gui library (dearimgui) that is compiled. To use it, include `gui_manager.hpp`, add `src/gui` subdirectory in your `cmake`, and link you executable against `cler_gui`.
See `freqplot` as an example.

* **Library**: </br>
There is also a library of useful blocks for quick plug and play in `src/blocks`. Because it is easy to create blocks, the DSP blocks can be specalizied for each case. As such, the library blocks are exactly the opposite, broad and general. There, we don't template where we dont have to. Everything that can go on the heap - goes on the heap.
See the gain block in `flowgraph.cpp` for an example