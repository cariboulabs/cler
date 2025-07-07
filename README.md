# Cler: Compile-Time DSP Flowgraph Framework

cler is a C++ heaer only template-based framework for constructing and executing flowgraphs of processing blocks, especially for DSP, SDR, or other high-throughput streaming pipelines.
Its goal is to be tiny, and allow maximal flexability:

* All code, no frontend.
* Defining blocks amounts to implementing a struct with a method.
* Channels are type agnostic.
* Blocks and flowgraph can be made completely static
* Tailored for Embedded Systems -  even MCUs

How to use it? Just Include `cler.hpp`

# Things to Know

* **Buffers** </br>
Our buffers are modified version of `https://github.com/drogalis/SPSC-Queue`. They allow for static or heap allocation. See  the gain block in `streamlined.cpp` for an example.

* **Peek-Commit or ReadWrite**: </br>
Cler supports three buffer access patterns: 
    * **Push/Pop** </br>
    For single values. there is also a try push/pop you can use if you dont inspect size() beforehand.

    * **Peek/Commit** </br>
    Allows you to inspect (peek) data in the buffer without removing it, then explicitly commit the number of items you’ve processed.
    The downside is that you can only access data up to the physical end of the ring buffer at a time — so if your logical window wraps, you may need to handle two chunks (the tail and the head).
    This means you might have to process or copy data in parts when the buffer wraps, adding some complexity.
    
    * **Read/Write**. </br>
    Provides access to the full available buffer space for larger chunks of data. You’ll typically copy data to a temporary buffer for processing. Read/Write automatically advances the ring buffer pointers for you — no manual commit needed.


* **Blocks**: </br>
Blocks is a library of useful blocks for quick plug and play Its depedencies are `liquid`, `imdeargui` and `zf_log`. In CLER, it is rather easy to create blocks for specific use cases. As such, the library blocks were decided to be exactly the opposite - broad and general. There, we don't optimize minimal work sizes, and we dont template where we dont have to. Everything that can go on the heap - goes on the heap. These blocks should be GENERAL for quick mockup tests.

* **Blocks/GUI**: </br>
Cler is a header only library, but includes a gui library (dearimgui) that is compiled. To use it, include `gui_manager.hpp` and link your executable against `cler_gui`.