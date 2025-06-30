# Cler: Compile-Time DSP Flowgraph Framework

cler is a C++ heaer only template-based framework for constructing and executing flowgraphs of processing blocks, especially for DSP, SDR, or other high-throughput streaming pipelines.
It is designed to maximize compile-time safety and performance while minimizing runtime overhead.

# Features

Tiny framework: Less than 1k lines of pure c++ code

Zero runtime polymorphism:
No virtual functions or base pointers. All block dispatch and connections are resolved statically.

Lock-free, high-performance channels:
Uses rigtorp::SPSCQueue for efficient single-producer/single-consumer buffering between blocks.

Template-based block construction:
Blocks define their own procedure_impl() with custom channel types and interfaces.

Automatic multithreading:
Each block runs in its own thread for maximum throughput and pipeline parallelism.