# Cler Performance Features Guide

This guide covers all performance enhancements available in Cler, with practical usage examples and guidance on when to use each feature.

## Overview

Cler provides two scheduler types with different performance characteristics:

1. **ThreadPerBlock** - Legacy scheduler (one thread per block)  
2. **FixedThreadPool** - Cache-optimized thread pool with round-robin block assignment

## Quick Start

### Basic Usage (Default Behavior)
```cpp
#include "cler.hpp"
#include "task_policies/cler_desktop_tpolicy.hpp"

// Default: ThreadPerBlock scheduler (legacy behavior)
auto flowgraph = cler::make_desktop_flowgraph(
    cler::BlockRunner(&source, &sink.in)
);
flowgraph.run();  // Uses ThreadPerBlock by default
```

### Enhanced Configuration
```cpp
// Enhanced configuration for performance optimization
cler::EnhancedFlowGraphConfig config;
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 4;
flowgraph.run(config);
```

## Scheduler Types

### 1. ThreadPerBlock (Legacy)
**When to use**: Simple flowgraphs, debugging, maximum compatibility

```cpp
// Explicit ThreadPerBlock configuration
cler::EnhancedFlowGraphConfig config;
config.scheduler = cler::SchedulerType::ThreadPerBlock;
config.adaptive_sleep = true;  // Enable adaptive sleep for CPU efficiency
flowgraph.run(config);

// Or use legacy FlowGraphConfig
cler::FlowGraphConfig legacy_config;
legacy_config.adaptive_sleep = true;
flowgraph.run(legacy_config);
```

**Characteristics:**
- One thread per block
- Simplest implementation
- Good for small flowgraphs (< 8 blocks)
- Highest thread overhead for large flowgraphs

### 2. FixedThreadPool 
**When to use**: Uniform workloads, embedded systems, predictable performance

```cpp
// Conservative embedded configuration
cler::EnhancedFlowGraphConfig embedded_config = cler::EnhancedFlowGraphConfig::embedded_optimized();
// Results in:
// - scheduler = FixedThreadPool
// - num_workers = 2 (conservative for embedded)
flowgraph.run(embedded_config);

// Desktop performance configuration  
cler::EnhancedFlowGraphConfig desktop_config = cler::EnhancedFlowGraphConfig::desktop_performance();
// Results in:
// - scheduler = FixedThreadPool  
// - num_workers = 4 (good default for desktop systems)
flowgraph.run(desktop_config);

// Manual configuration
cler::FlowGraphConfig manual_config;
manual_config.scheduler = cler::SchedulerType::FixedThreadPool;
manual_config.num_workers = 4;                    // Explicit worker count
flowgraph.run(manual_config);
```

**Characteristics:**
- Fixed number of worker threads
- Round-robin block assignment  
- Best for uniform workloads
- +30-40% improvement over ThreadPerBlock
- Predictable resource usage


## Performance Benchmarks

### Test Results (256M samples, 4-stage pipeline)

**Uniform Workload (all blocks similar complexity):**
```
Legacy ThreadPerBlock:           404.0 MSamples/sec (baseline)
Enhanced (2 workers, safe):      390.3 MSamples/sec (-3.4%)
Enhanced (4 workers, optimized): 526.9 MSamples/sec (+30.4%) 🏆
Enhanced (auto workers):         428.3 MSamples/sec (+6.0%)
Adaptive Load Balancing:         341.3 MSamples/sec (-15.5%)
```

**Variable Workload (blocks with different complexity):**
```
ThreadPerBlock:                  17.7 MSamples/sec (baseline)
FixedThreadPool (4 workers):     42.2 MSamples/sec (+138%) 🏆
FixedThreadPool (8 workers):     45.1 MSamples/sec (+154%)
FixedThreadPool + adaptive sleep: 48.3 MSamples/sec (+173%)
```

## Decision Matrix

### Choose ThreadPerBlock when:
- ✅ Small flowgraphs (< 4 blocks)
- ✅ Debugging or development
- ✅ Maximum compatibility needed
- ✅ Simple, predictable workloads
- ❌ Large flowgraphs (> 8 blocks)
- ❌ Performance is critical

### Choose FixedThreadPool when:
- ✅ Uniform block complexity
- ✅ Embedded systems (predictable resources)
- ✅ Medium to large flowgraphs (4-20 blocks)
- ✅ Consistent performance needed
- ✅ Good balance of performance and simplicity
- ❌ Highly imbalanced workloads


## Code Examples

### Example 1: Simple DSP Pipeline
```cpp
// For uniform DSP processing (filters, gains, etc.)
// Recommendation: FixedThreadPool
cler::EnhancedFlowGraphConfig config;
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 4;
```

### Example 2: Mixed Processing Pipeline
```cpp
// Pipeline with: FFT -> filtering -> detection -> protocol decode
// Some blocks are CPU-heavy (FFT), others are light (protocol)
// Recommendation: FixedThreadPool with adaptive sleep
cler::FlowGraphConfig config;
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 6;  // More workers for complex pipeline
config.adaptive_sleep = true;  // Help with imbalanced workload
```

### Example 3: Embedded SDR
```cpp
// Embedded system with limited cores and memory
// Recommendation: Embedded-optimized FixedThreadPool
cler::EnhancedFlowGraphConfig config = cler::EnhancedFlowGraphConfig::embedded_optimized();
// This gives: FixedThreadPool, 2 workers, safe error checking
```

### Example 4: Real-time Protocol Processing
```cpp
// Protocol processing with timing constraints
// Some packets need heavy processing, others are simple
// Recommendation: FixedThreadPool (conservative)
cler::FlowGraphConfig config;
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 4;
config.adaptive_sleep = true;  // Help with timing constraints
```

## Factory Methods

Cler provides factory methods for common configurations:

```cpp
// Embedded-optimized (conservative, safe)
auto config = cler::EnhancedFlowGraphConfig::embedded_optimized();

// Desktop performance (aggressive optimizations)
auto config = cler::EnhancedFlowGraphConfig::desktop_performance();

// Adaptive load balancing (dynamic workloads)
auto config = cler::EnhancedFlowGraphConfig::work_stealing();
```

## Monitoring Performance

### Enable Statistics Collection
```cpp
// All configurations automatically collect statistics
flowgraph.run(config);

// After stopping, access detailed statistics
flowgraph.stop();
const auto& stats = flowgraph.stats();

for (const auto& stat : stats) {
    std::cout << stat.name.c_str() << ": "
              << stat.successful_procedures << " calls, "
              << stat.total_runtime_s << "s runtime" << std::endl;
}
```

### Identify Bottlenecks
```cpp
// Look for blocks with:
// 1. High total_runtime_s (CPU bottlenecks)
// 2. High failed_procedures (I/O bottlenecks) 
// 3. High avg_dead_time_us (waiting for data)

// High runtime suggests this block needs more workers (load balancing helps)
// High failures suggest buffer size or producer/consumer rate mismatch
// High dead time suggests this block is waiting (not the bottleneck)
```

## Advanced Configuration

### Custom Worker Count
```cpp
cler::EnhancedFlowGraphConfig config;
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 8;  // Explicit worker count (not limited to 4)
// Note: Only auto-detection is limited to 4 for embedded compatibility
```

### Fine-tuning Load Balancing
```cpp
cler::EnhancedFlowGraphConfig config = cler::EnhancedFlowGraphConfig::work_stealing();

// For workloads that change rapidly
config.rebalance_interval = 100;    // Check every 100 procedure calls
config.load_balance_threshold = 0.05; // Very sensitive (5% imbalance)

// For workloads that change slowly  
config.rebalance_interval = 5000;   // Check every 5000 procedure calls
config.load_balance_threshold = 0.4; // Less sensitive (40% imbalance)
```

### Combining Optimizations
```cpp
cler::EnhancedFlowGraphConfig config;
config.scheduler = cler::SchedulerType::FixedThreadPool;
config.num_workers = 6;
config.enable_load_balancing = true;
config.rebalance_interval = 300;
config.load_balance_threshold = 0.15;
// This combines thread pooling + load balancing
```

## Running the Performance Test

```bash
cd performance
g++ -std=c++17 -I ../include -I ../include/task_policies -O3 -o performance_features performance_features.cpp -pthread
./performance_features
```

The test will show incremental improvements:
1. Legacy ThreadPerBlock (baseline)
2. Enhanced FixedThreadPool (conservative)
3. Enhanced FixedThreadPool (optimized) 
4. Enhanced FixedThreadPool (auto workers)
5. Adaptive Load Balancing (default)
6. Adaptive Load Balancing (aggressive)

## Troubleshooting

### Poor Performance with Load Balancing
- **Cause**: Uniform workload doesn't benefit from load balancing
- **Solution**: Use FixedThreadPool instead

### High CPU Usage
- **Cause**: Too many workers or aggressive rebalancing
- **Solution**: Reduce `num_workers` or increase `rebalance_interval`

### Inconsistent Performance  
- **Cause**: Load balancing adapting to changing workload
- **Solution**: Increase `load_balance_threshold` for more stability

### Low Performance on Embedded
- **Cause**: Too many workers for available cores
- **Solution**: Use `embedded_optimized()` factory method

---

**Remember**: Always profile your specific workload. These guidelines are based on typical use cases, but your specific blocks and data patterns may behave differently.