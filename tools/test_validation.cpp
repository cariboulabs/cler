// Test file for Cler validator - contains intentional errors for testing
#include "cler.hpp"
#include "cler_desktop_utils.hpp"

// Example blocks (simplified for testing)
template<typename T>
struct SourceBlock : public cler::BlockBase {
    SourceBlock(const char* name, T amplitude) 
        : cler::BlockBase(name), _amplitude(amplitude) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Implementation details...
        return cler::Empty{};
    }
private:
    T _amplitude;
};

template<typename T>
struct GainBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    GainBlock(const char* name, T gain) 
        : cler::BlockBase(name), in(256), _gain(gain) {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<T>* out) {
        // Implementation details...
        return cler::Empty{};
    }
private:
    T _gain;
};

template<typename T>
struct SinkBlock : public cler::BlockBase {
    cler::Channel<T> in;
    
    SinkBlock(const char* name) 
        : cler::BlockBase(name), in(256) {}
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        // Implementation details...
        return cler::Empty{};
    }
};

int main() {
    // TEST CASE 1: Missing BlockRunner
    // This source block is declared but has no BlockRunner
    SourceBlock<float> source1("Source1", 1.0f);
    
    // TEST CASE 2: BlockRunner not in flowgraph  
    // This gain block has a runner but it's not added to the flowgraph
    GainBlock<float> gain1("Gain1", 2.0f);
    
    // TEST CASE 3: Invalid connection reference
    // This references a non-existent block 'nonexistent'
    SourceBlock<float> source2("Source2", 0.5f);
    
    // TEST CASE 4: Duplicate block name (variable)
    GainBlock<float> gain2("Gain2", 1.5f);
    GainBlock<float> gain2("Gain2_duplicate", 3.0f);  // Duplicate variable name!
    
    // TEST CASE 5: Unconnected input warning
    // This sink has no input connection
    SinkBlock<float> sink1("Sink1");
    
    // Correct usage for comparison
    SourceBlock<float> source3("Source3", 1.0f);
    GainBlock<float> gain3("Gain3", 2.0f);
    SinkBlock<float> sink2("Sink2");
    
    // Create flowgraph with some runners missing
    auto flowgraph = cler::make_desktop_flowgraph(
        // source1 - Missing runner entirely
        cler::BlockRunner(&gain1, &sink1.in),  // gain1 runner exists but...
        cler::BlockRunner(&source2, &nonexistent.in),  // Invalid connection!
        cler::BlockRunner(&source3, &gain3.in),
        cler::BlockRunner(&gain3, &sink2.in),
        cler::BlockRunner(&sink2)
        // Note: gain1's runner is created but not added to flowgraph
    );
    
    flowgraph.run();
    
    return 0;
}