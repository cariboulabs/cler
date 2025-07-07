# Cler: Compile-Time DSP Flowgraph Framework

Cler is a C++ header only template-based framework for constructing and executing flowgraphs of DSP processing blocks.
Its goal is to be tiny, and allow maximal flexability:

* All code, no frontend.
* Defining blocks amounts to implementing a struct with a method.
* Channels are type agnostic.
* Flowgraphs are, and Blocks can be made completely static
* Tailored for Embedded Systems -  even MCUs
* Built for radio, but can also be used for control and dynamic simulations (supports cyclic graphs, and online modifiable params)

How to use it? Just Include `cler.hpp` and you are good for the basics.
Want to use included blocks? See the `examples` folder.

Just one thing to look out for... because Cler is template heavy, error messages can overwhelming. But no worries, with the small context window that is Cler, any LLM can help you out with ease.

# Things to Know

* **Buffers** </br>
Our buffers are modified version of `https://github.com/drogalis/SPSC-Queue`. They allow for static or heap allocation. See  the gain block in `streamlined.cpp` for an example.

* **Peek-Commit or ReadWrite**: </br>
Cler supports three buffer access patterns: 
    * **Push/Pop** </br>
    For single values. there is also a try push/pop you can use if you dont inspect size() beforehand.
    Remember though, after you have poped a value, you must not put it back! Cler channels are lock-free SPSC that *ASSUME* that one thread is a writer while another is a reader. No mixin' it up

    * **Peek/Commit** </br>
    Allows you to inspect (peek) data in the buffer without removing it, then explicitly commit the number of items you’ve processed.
    The downside is that you can only access data up to the physical end of the ring buffer at a time — so if your logical window wraps, you may need to handle two chunks (the tail and the head).
    This means you might have to process or copy data in parts when the buffer wraps, adding some complexity.
    
    * **Read/Write**. </br>
    Provides access to the full available buffer space for larger chunks of data. You’ll typically copy data to a temporary buffer for processing. Read/Write automatically advances the ring buffer pointers for you — no manual commit needed.

* **Flowgraph vs Streamlined** </br>
Cler supports two architectural styles:
    * **flowgraph** </br>
    In flowgraph mode, you manually define each block and the channels that connect them. You then pass the connection structure into a flowgraph that is incharge of running the blocks. Behind the scenees, Cler flowgraph creates an OS thread for every block which constantly calls its `procedure()`. When the call returns an error, it yeilds to other threads before trying again.

    * **streamlined** </br>
    In streamlined mode, you are in charge of writing the loop, and you are in charge of passing samples from one block to the other.

    When the blocks are simple, the streamlined approach will be faster than the flowgraph becuase of the thread overhead. As a compromise, you can create `superblocks` which combine multiple small blocks.
    See `streamlined` and  `flowgraph` examples.



* **Blocks**: </br>
Blocks is a library of useful blocks for quick "plug and play". Its soft depedencies are `liquid`, `imdeargui` and `zf_log`. </br>
In CLER, it is rather easy to create blocks for specific use cases. As such, the library blocks were decided to be exactly the opposite - broad and general. There, we don't optimize minimal work sizes, and we dont template where we dont have to. Everything that can go on the heap - goes on the heap. These blocks should be GENERAL for quick mockup tests.

* **Blocks/GUI**: </br>
Cler is a header only library, but includes a gui library (dearimgui) that is compiled. To use it, include `gui_manager.hpp` and link your executable against `cler_gui`. See the `plots` or `mass-spring-damper` examples.