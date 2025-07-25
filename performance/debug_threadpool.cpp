#include "cler.hpp"
#include "cler_utils.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"
#include <iostream>
#include <chrono>

// Simple source that always has data
struct FastSource : public cler::BlockBase {
    FastSource() : BlockBase("FastSource") {}
    
    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        float buffer[1024];
        std::fill(buffer, buffer + 1024, 1.0f);
        out->writeN(buffer, 1024);
        return cler::Empty{};
    }
};

// Simple sink that always consumes
struct FastSink : public cler::BlockBase {
    cler::Channel<float> in;
    size_t count = 0;
    
    FastSink() : BlockBase("FastSink"), in(2048) {}
    
    cler::Result<cler::Empty, cler::Error> procedure() {
        size_t available = in.size();
        if (available > 0) {
            in.commit_read(available);
            count += available;
        }
        return cler::Empty{};
    }
};

int main() {
    std::cout << "=== Thread Pool Debug Test ===" << std::endl;
    
    // Test 1: Single source->sink with ThreadPerBlock
    {
        FastSource source;
        FastSink sink;
        
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );
        
        auto start = std::chrono::steady_clock::now();
        fg.run_for(std::chrono::seconds(2));
        auto end = std::chrono::steady_clock::now();
        
        double duration = std::chrono::duration<double>(end - start).count();
        double throughput = sink.count / duration;
        
        std::cout << "ThreadPerBlock (2 blocks): " << (throughput/1e6) << " MSamples/sec" << std::endl;
    }
    
    // Test 2: Same with FixedThreadPool
    {
        FastSource source;
        FastSink sink;
        
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source, &sink.in),
            cler::BlockRunner(&sink)
        );
        
        cler::FlowGraphConfig config;
        config.scheduler = cler::SchedulerType::FixedThreadPool;
        config.num_workers = 2;
        
        auto start = std::chrono::steady_clock::now();
        fg.run_for(std::chrono::seconds(2), config);
        auto end = std::chrono::steady_clock::now();
        
        double duration = std::chrono::duration<double>(end - start).count();
        double throughput = sink.count / duration;
        
        std::cout << "FixedThreadPool (2 workers): " << (throughput/1e6) << " MSamples/sec" << std::endl;
    }
    
    // Test 3: 4 blocks with ThreadPerBlock
    {
        FastSource source1, source2;
        FastSink sink1, sink2;
        
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source1, &sink1.in),
            cler::BlockRunner(&source2, &sink2.in),
            cler::BlockRunner(&sink1),
            cler::BlockRunner(&sink2)
        );
        
        auto start = std::chrono::steady_clock::now();
        fg.run_for(std::chrono::seconds(2));
        auto end = std::chrono::steady_clock::now();
        
        double duration = std::chrono::duration<double>(end - start).count();
        double throughput = (sink1.count + sink2.count) / duration;
        
        std::cout << "ThreadPerBlock (4 blocks): " << (throughput/1e6) << " MSamples/sec" << std::endl;
    }
    
    // Test 4: Same with FixedThreadPool
    {
        FastSource source1, source2;
        FastSink sink1, sink2;
        
        auto fg = cler::make_desktop_flowgraph(
            cler::BlockRunner(&source1, &sink1.in),
            cler::BlockRunner(&source2, &sink2.in),
            cler::BlockRunner(&sink1),
            cler::BlockRunner(&sink2)
        );
        
        cler::FlowGraphConfig config;
        config.scheduler = cler::SchedulerType::FixedThreadPool;
        config.num_workers = 2;
        
        auto start = std::chrono::steady_clock::now();
        fg.run_for(std::chrono::seconds(2), config);
        auto end = std::chrono::steady_clock::now();
        
        double duration = std::chrono::duration<double>(end - start).count();
        double throughput = (sink1.count + sink2.count) / duration;
        
        std::cout << "FixedThreadPool (2 workers, 4 blocks): " << (throughput/1e6) << " MSamples/sec" << std::endl;
    }
    
    return 0;
}