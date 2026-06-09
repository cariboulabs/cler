// Scheduler latency stress benchmark.
//
// Companion to docs/scheduler_latency_stress_plan.md and
// docs/scheduler_latency_impl_plan.md. Measures enqueue-to-pop latency of a
// low-rate message pipeline under different schedulers / idle behavior, rather
// than the throughput that the other perf_* benchmarks target.
//
// Output is line-oriented and diffable, one line per (case, config):
//   case=low_rate scheduler=thread_per_block idle=busy period_us=1000 ...
//
// Usage: perf_scheduler_latency [case]
//   case = low_rate | all   (more cases land in later phases)

#include "cler.hpp"
#include "cler_utils.hpp"
#include "cler_desktop_utils.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using steady = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Message type: carries a sequence number, the steady-clock timestamp at the
// moment it was enqueued, and a small payload to keep the item non-trivial.
// ---------------------------------------------------------------------------
struct Msg {
    uint64_t sequence = 0;
    steady::time_point enqueue_ts{};
    float payload[16] = {};
};

// ---------------------------------------------------------------------------
// Latency histogram: collect raw samples, compute percentiles at the end.
// Single-threaded ownership (the sink block) — no locking needed.
// ---------------------------------------------------------------------------
struct LatencyStats {
    std::vector<double> samples_us;

    void record(double us) { samples_us.push_back(us); }

    double percentile(double p) {
        if (samples_us.empty()) return 0.0;
        // assumes samples_us already sorted
        size_t idx = static_cast<size_t>((p / 100.0) * (samples_us.size() - 1));
        return samples_us[idx];
    }

    void finalize() { std::sort(samples_us.begin(), samples_us.end()); }
    size_t count() const { return samples_us.size(); }
};

// ---------------------------------------------------------------------------
// Periodic source: emits one Msg every period_us. Returns NotEnoughSamples
// (the current no-progress signal) when it is not yet time to emit, so it does
// NOT lie to the scheduler about having done work (idle-contract clean).
// ---------------------------------------------------------------------------
struct PeriodicMessageSource : public cler::BlockBase {
    PeriodicMessageSource(std::string name, uint64_t period_us)
        : BlockBase(std::move(name)),
          _period(std::chrono::microseconds(period_us)),
          _next(steady::now()) {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Msg>* out) {
        auto now = steady::now();
        if (now < _next) {
            return cler::Error::NotEnoughSamples;  // not yet time -> no work
        }

        Msg m;
        m.sequence = _sequence;
        m.enqueue_ts = steady::now();
        if (!out->try_push(m)) {
            return cler::Error::NotEnoughSpace;  // backpressure
        }

        _sequence++;
        _next += _period;
        // If we fell badly behind (e.g. sink stalled), don't spiral: catch up.
        if (now - _next > _period * 4) {
            _next = now + _period;
        }
        return cler::Empty{};
    }

    uint64_t emitted() const { return _sequence; }

private:
    std::chrono::microseconds _period;
    steady::time_point _next;
    uint64_t _sequence = 0;
};

// ---------------------------------------------------------------------------
// Message sink: pops up to a bounded batch per procedure and records
// enqueue-to-pop latency in microseconds.
// ---------------------------------------------------------------------------
struct MessageSink : public cler::BlockBase {
    cler::Channel<Msg> in;
    LatencyStats stats;

    MessageSink(std::string name, size_t channel_size = 4096)
        : BlockBase(std::move(name)), in(channel_size) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        static constexpr size_t MAX_BATCH = 16;
        Msg m;
        size_t popped = 0;
        while (popped < MAX_BATCH && in.try_pop(m)) {
            auto now = steady::now();
            double us = std::chrono::duration<double, std::micro>(now - m.enqueue_ts).count();
            stats.record(us);
            popped++;
        }
        if (popped == 0) return cler::Error::NotEnoughSamples;
        return cler::Empty{};
    }
};

// ---------------------------------------------------------------------------
// One low_rate measurement.
// ---------------------------------------------------------------------------
struct CaseConfig {
    const char* idle_label;
    const char* sched_label;
    cler::FlowGraphConfig cfg;
};

static void run_low_rate(const CaseConfig& cc, uint64_t period_us,
                         std::chrono::seconds duration) {
    PeriodicMessageSource source("Source", period_us);
    MessageSink sink("Sink");

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    fg.run_for(duration, cc.cfg);

    sink.stats.finalize();
    double secs = static_cast<double>(duration.count());
    double thr = secs > 0.0 ? sink.stats.count() / secs : 0.0;

    printf("case=low_rate scheduler=%s idle=%s period_us=%llu items=%zu "
           "thr_ips=%.1f p50_us=%.2f p90_us=%.2f p99_us=%.2f max_us=%.2f\n",
           cc.sched_label, cc.idle_label,
           static_cast<unsigned long long>(period_us),
           sink.stats.count(), thr,
           sink.stats.percentile(50), sink.stats.percentile(90),
           sink.stats.percentile(99), sink.stats.percentile(100));
    fflush(stdout);
}

static void case_low_rate() {
    const std::chrono::seconds duration(3);
    const uint64_t periods_us[] = {100, 1000, 10000, 100000};

    std::vector<CaseConfig> configs;

    // ThreadPerBlock default (busy retry, adaptive sleep off).
    configs.push_back({"busy", "thread_per_block", cler::FlowGraphConfig{}});

    // ThreadPerBlock with adaptive sleep.
    configs.push_back({"adaptive", "thread_per_block",
                       cler::flowgraph_config::thread_per_block_adaptive_sleep()});

    // FixedThreadPool, 2 workers.
    configs.push_back({"relax", "fixed_pool_2",
                       cler::flowgraph_config::embedded_optimized()});

    // FixedThreadPool, 4 workers.
    configs.push_back({"relax", "fixed_pool_4",
                       cler::flowgraph_config::desktop_performance()});

    // FixedThreadPool, 4 workers, pinned.
    {
        auto pinned = cler::flowgraph_config::desktop_performance();
        pinned.pin_workers = true;
        configs.push_back({"relax", "fixed_pool_4_pinned", pinned});
    }

    for (const auto& cc : configs) {
        for (uint64_t p : periods_us) {
            run_low_rate(cc, p, duration);
        }
    }
}

int main(int argc, char** argv) {
    std::string which = (argc > 1) ? argv[1] : "all";

    if (which == "low_rate" || which == "all") {
        case_low_rate();
    }

    return 0;
}
