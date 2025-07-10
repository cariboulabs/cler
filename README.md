# Cler: Compile-Time DSP Flowgraph Framework

Cler is a C++ header only template-based framework for constructing and executing flowgraphs of DSP processing blocks.
Its goal is to be tiny, and allow maximal flexability:

* All code, no frontend.
* Defining blocks amounts to implementing a struct with a method.
* Channels are type agnostic.
* Flowgraphs are, and Blocks can be made completely static
* Tailored for Embedded Systems -  even MCUs
* Built for radio, but can also be used for control and dynamic simulations (supports cyclic graphs, and online modifiable params)
* Cross-Platform (Linux, Windows and MacOS)

How to use it? Just Include `cler.hpp` and you are good for the basics.
Want to use included blocks? See the `examples` folder.

Just one thing to look out for... because Cler is template heavy, error messages can be overwhelming. But no worries, with the small context window that is Cler, any LLM can help you out with ease.

# Things to Know

* **Buffers** </br>
Our buffers are modified version of `https://github.com/drogalis/SPSC-Queue`. They allow for static or heap allocation. See  the gain block in `streamlined.cpp` for an example.

* **Peek-Commit or ReadWrite**: </br>
Cler supports three buffer access patterns: 
    * **Push/Pop** </br>
    For single values. there is also a try push/pop you can use if you dont inspect size() beforehand.
    Remember though, after you have poped a value, you must not put it back! Cler channels are lock-free SPSC that *ASSUME* that one thread is a writer while another is a reader. No mixin' it up. </br>
    Also, this is *SLOW*. Always prefer the other access patterns.

    * **Peek/Commit** </br>
    Allows you to inspect (peek) data in the buffer without removing it, then explicitly commit the number of items you’ve processed.
    The downside is that you can only access data up to the physical end of the ring buffer at a time — so if your logical window wraps, you may need to handle two chunks.
    
    * **Read/Write**. </br>
    Provides access to the full available buffer space for larger chunks of data. You’ll typically copy data to a temporary buffer for processing. Read/Write automatically advances the ring buffer pointers for you — no manual commit needed. This should be your *go to* pattern.

* **Flowgraph vs Streamlined** </br>
Cler supports two architectural styles:
    * **flowgraph** </br>
    In flowgraph mode, you manually define each block and the channels that connect them. You then pass the connection structure into a flowgraph that is incharge of running the blocks. Behind the scenees, Cler flowgraph creates an OS thread for every block which constantly calls its `procedure()`. When the call returns an error, it yeilds to other threads before trying again.

    * **streamlined** </br>
    In streamlined mode, you are in charge of writing the loop, and you are in charge of passing samples from one block to the other.

    When the blocks are simple, the streamlined approach will be faster than the flowgraph becuase of the thread overhead. As a compromise, you can create `superblocks` which combine multiple small blocks.
    See `streamlined` and  `flowgraph` as an examples for the two architectural styles, and `polyphase_channelizer` for a superblock implementation



* **Blocks**: </br>
Blocks is a library of useful blocks for quick "plug and play". Its soft depedencies are `liquid`, `imdeargui` and `zf_log` brought in by CMAKE's fetch content. </br>
In CLER, it is rather easy to create blocks for specific use cases. As such, the library blocks were decided to be exactly the opposite - broad and general. There, we don't optimize minimal work sizes, and we dont template where we dont have to. Everything that can go on the heap - goes on the heap. These blocks should be GENERAL for quick mockup tests.

* **Blocks/GUI**: </br>
Cler is a header only library, but includes a gui library (dearimgui) that is compiled. To use it, include `gui_manager.hpp` and link your executable against `cler_gui`. See the `plots` or `mass_spring_damper` examples.

# RoadMap
* <ins>Flowgraph validation:</ins><br/>
To keep the blocks/channels structure free of overbearing boilerplate validation logic, the best approach is to create an external tool that analyzes the application’s C++ code and validates it:
   - Do all blocks have runners?
   - Are all runners provided to the flowgraph?

    Additionally, we could develop a VS Code extension to automate these checks.

* <ins>Comparing to GnuRadio / FutureSDR:</ins> </br>
Its important that we know where we stand. We need to measure our performence against the best in the buissness and produce a report.

* <ins>Testing / CI:</ins> </br>
If we are already producing a report 

* <ins>GUI FrontEnd:</ins> </br>
While not a preference, if we are already creating a reflection tool for FlowGraph validation, we could also create an interactive FlowGraph generator. Could be some Desktop Application, that scans the /blocks folders, generates an interface markup file for each block, and then uses this information to allow the user to connect blocks on a canvas.
Importat:
    - Has to be cross platform.
    - Will not force blocks to implement markup files. Has to be generated.

* <ins>Hardware Support:</ins> </br>
If we are serious about this, we need to support workflows that can process Wi-Fi in both the 2.4 GHz and 5.2 GHz bands. For this, we must ensure support for commodity hardware. So introducing source/sink blocks for these devices is welome.

* <ins>GPU Support:</ins> </br>
GPU can be instrumental on processing higher volumes. Creating ChannelGPU which uses the ChannelBase interface would allow users to write their GPU blocks.