// This file should PASS validation - streamlined mode (no flowgraph)
#include "cler.hpp"

struct SimpleSource : public cler::BlockBase {
    SimpleSource(const char* name) : BlockBase(name) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::Channel<float>* out) {
        float value = 1.0f;
        out->push(value);
        return cler::Empty{};
    }
};

struct SimpleSink : public cler::BlockBase {
    cler::Channel<float> in;
    
    SimpleSink(const char* name) : BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        float value;
        if (in.try_pop(value)) {
            // Process value
        }
        return cler::Empty{};
    }
};

int main() {
    // Streamlined mode - no BlockRunners or flowgraph needed
    SimpleSource source("Source");
    SimpleSink sink("Sink");
    
    while (true) {
        source.procedure(&sink.in);
        sink.procedure();
    }
    
    return 0;
}