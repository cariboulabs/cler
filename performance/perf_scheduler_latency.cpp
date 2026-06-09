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
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/resource.h>

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
//
// Samples are capped (and the buffer reserved up front) so a fast pipeline
// pushing tens of millions of items does not realloc inside the sink loop and
// perturb the very tail latency we are trying to measure. `total` tracks the
// true delivered count for throughput; percentiles use the capped sample set,
// which is representative for these constant-rate benchmarks.
// ---------------------------------------------------------------------------
struct LatencyStats {
    static constexpr size_t CAP = 2'000'000;  // ~16 MB of doubles
    std::vector<double> samples_us;
    uint64_t total = 0;

    void record(double us) {
        if (samples_us.capacity() == 0) samples_us.reserve(CAP);
        total++;
        if (samples_us.size() < CAP) samples_us.push_back(us);
    }

    double percentile(double p) {
        if (samples_us.empty()) return 0.0;
        // assumes samples_us already sorted
        size_t idx = static_cast<size_t>((p / 100.0) * (samples_us.size() - 1));
        return samples_us[idx];
    }

    void finalize() { std::sort(samples_us.begin(), samples_us.end()); }
    uint64_t count() const { return total; }
};

// ---------------------------------------------------------------------------
// Process-wide CPU meter. Reports CPU-core-seconds consumed per wall second,
// i.e. cpu_pct=100 means one full core, 800 means eight. This is the axis the
// idle-policy tradeoff lives on: busy-spin buys low latency by burning cores,
// and latency numbers alone cannot see that cost. RUSAGE_SELF aggregates all
// threads (Linux/desktop perf target).
// ---------------------------------------------------------------------------
struct CpuMeter {
    static double proc_cpu_s() {
        rusage r{};
        getrusage(RUSAGE_SELF, &r);
        auto tv = [](const timeval& t) { return t.tv_sec + t.tv_usec * 1e-6; };
        return tv(r.ru_utime) + tv(r.ru_stime);
    }

    void start() { _cpu0 = proc_cpu_s(); _wall0 = steady::now(); }

    double cpu_percent() const {
        double cpu = proc_cpu_s() - _cpu0;
        double wall = std::chrono::duration<double>(steady::now() - _wall0).count();
        return wall > 0.0 ? (cpu / wall) * 100.0 : 0.0;
    }
private:
    double _cpu0 = 0.0;
    steady::time_point _wall0{};
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

    CpuMeter cpu;
    cpu.start();
    fg.run_for(duration, cc.cfg);
    double cpu_pct = cpu.cpu_percent();

    sink.stats.finalize();
    double secs = static_cast<double>(duration.count());
    double thr = secs > 0.0 ? sink.stats.count() / secs : 0.0;

    printf("case=low_rate scheduler=%s idle=%s period_us=%llu items=%llu "
           "thr_ips=%.1f cpu_pct=%.0f p50_us=%.2f p90_us=%.2f p99_us=%.2f max_us=%.2f\n",
           cc.sched_label, cc.idle_label,
           static_cast<unsigned long long>(period_us),
           static_cast<unsigned long long>(sink.stats.count()), thr, cpu_pct,
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

// ---------------------------------------------------------------------------
// Case 2: CPU-contended message pipeline.
//
// A 1ms message pipeline runs alongside N busy DSP pipelines
// (BusySource -> BusyCopy -> BusySink). The question is whether the unrelated
// hot blocks delay message delivery (p99/max latency).
// ---------------------------------------------------------------------------
enum class BusyWork { None, Memcpy, Heavy };

static const char* work_label(BusyWork w) {
    switch (w) {
        case BusyWork::None:   return "none";
        case BusyWork::Memcpy: return "memcpy";
        case BusyWork::Heavy:  return "heavy";
    }
    return "?";
}

constexpr size_t BUSY_CH = 512;

// Burn the configured amount of work, then return one float of "result".
static inline float do_busy_work(BusyWork w, float in, float* scratch, size_t n) {
    switch (w) {
        case BusyWork::None:
            return in;
        case BusyWork::Memcpy:
            std::memset(scratch, 0, n * sizeof(float));
            scratch[0] = in;
            return scratch[0];
        case BusyWork::Heavy: {
            float acc = in;
            for (size_t i = 0; i < 256; ++i) acc = std::sqrt(acc * 1.0001f + 1.0f);
            return acc;
        }
    }
    return in;
}

struct BusySource : public cler::BlockBase {
    BusyWork work = BusyWork::None;
    BusySource() : BlockBase("busy_src") {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        if (!out->try_push(_v)) return cler::Error::NotEnoughSpace;
        _v += 1.0f;
        return cler::Empty{};
    }
private:
    float _v = 1.0f;
};

struct BusyCopy : public cler::BlockBase {
    cler::Channel<float, BUSY_CH> in;
    BusyWork work = BusyWork::None;
    BusyCopy() : BlockBase("busy_copy") {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        float v;
        if (!in.try_pop(v)) return cler::Error::NotEnoughSamples;
        v = do_busy_work(work, v, _scratch, 256);
        if (!out->try_push(v)) return cler::Error::NotEnoughSpace;
        return cler::Empty{};
    }
private:
    float _scratch[256] = {};
};

struct BusySink : public cler::BlockBase {
    cler::Channel<float, BUSY_CH> in;
    BusySink() : BlockBase("busy_sink") {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        float v;
        if (!in.try_pop(v)) return cler::Error::NotEnoughSamples;
        _sink += v;  // keep the optimizer honest
        return cler::Empty{};
    }
private:
    volatile float _sink = 0.0f;
};

template <size_t N, size_t... I>
static void run_contended_impl(BusyWork work, uint64_t period_us,
                               std::chrono::seconds duration,
                               std::index_sequence<I...>) {
    PeriodicMessageSource msrc("MsgSrc", period_us);
    MessageSink msink("MsgSink");
    std::array<BusySource, N> bsrc;
    std::array<BusyCopy, N> bcopy;
    std::array<BusySink, N> bsink;
    for (size_t i = 0; i < N; ++i) { bsrc[i].work = work; bcopy[i].work = work; }
    (void)bsrc; (void)bcopy; (void)bsink;  // silence unused warning for N=0

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&msrc, &msink.in),
        cler::BlockRunner(&msink),
        cler::BlockRunner(&bsrc[I], &bcopy[I].in)...,
        cler::BlockRunner(&bcopy[I], &bsink[I].in)...,
        cler::BlockRunner(&bsink[I])...
    );

    CpuMeter cpu;
    cpu.start();
    fg.run_for(duration);  // default ThreadPerBlock, busy idle
    double cpu_pct = cpu.cpu_percent();

    msink.stats.finalize();
    double secs = static_cast<double>(duration.count());
    double thr = secs > 0.0 ? msink.stats.count() / secs : 0.0;

    printf("case=contended scheduler=thread_per_block idle=busy busy_pipes=%zu "
           "work=%s period_us=%llu items=%llu thr_ips=%.1f cpu_pct=%.0f "
           "p50_us=%.2f p90_us=%.2f p99_us=%.2f max_us=%.2f\n",
           N, work_label(work), static_cast<unsigned long long>(period_us),
           static_cast<unsigned long long>(msink.stats.count()), thr, cpu_pct,
           msink.stats.percentile(50), msink.stats.percentile(90),
           msink.stats.percentile(99), msink.stats.percentile(100));
    fflush(stdout);
}

template <size_t N>
static void run_contended(BusyWork work, uint64_t period_us,
                          std::chrono::seconds duration) {
    run_contended_impl<N>(work, period_us, duration, std::make_index_sequence<N>{});
}

static void case_contended() {
    const std::chrono::seconds duration(3);
    const uint64_t period_us = 1000;  // 1ms message rate

    for (BusyWork w : {BusyWork::None, BusyWork::Memcpy, BusyWork::Heavy}) {
        run_contended<0>(w, period_us, duration);
        run_contended<1>(w, period_us, duration);
        run_contended<2>(w, period_us, duration);
        run_contended<4>(w, period_us, duration);
        run_contended<8>(w, period_us, duration);
    }
}

// ---------------------------------------------------------------------------
// Case 3: Slow terminal sink.
//
// A burst source that pushes as fast as it can feeds a slow sink through a
// bounded queue. Models a blocking socket / slow file / stalled downstream.
// Phase 0 measures only the current FIFO-with-backpressure behavior: high
// delivery, but item age grows to (queue_depth * sink_period) under stall.
// Freshness policies (drop-newest / keep-latest / TTL) are compared in Phase 4.
// ---------------------------------------------------------------------------
struct BurstMessageSource : public cler::BlockBase {
    BurstMessageSource() : BlockBase("BurstSrc") {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<Msg>* out) {
        Msg m;
        m.sequence = _sequence;
        m.enqueue_ts = steady::now();
        if (!out->try_push(m)) return cler::Error::NotEnoughSpace;  // backpressure
        _sequence++;
        return cler::Empty{};
    }
private:
    uint64_t _sequence = 0;
};

struct SlowSink : public cler::BlockBase {
    cler::Channel<Msg> in;
    LatencyStats stats;

    SlowSink(uint64_t sleep_us, bool periodic_stall, size_t channel_size = 1024)
        : BlockBase("SlowSink"), in(channel_size),
          _sleep(std::chrono::microseconds(sleep_us)),
          _periodic_stall(periodic_stall) {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        Msg m;
        if (!in.try_pop(m)) return cler::Error::NotEnoughSamples;

        auto now = steady::now();
        stats.record(std::chrono::duration<double, std::micro>(now - m.enqueue_ts).count());

        _popped++;
        if (_periodic_stall && (_popped % 500 == 0)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else if (_sleep.count() > 0) {
            std::this_thread::sleep_for(_sleep);
        }
        return cler::Empty{};
    }

    uint64_t popped() const { return _popped; }
private:
    std::chrono::microseconds _sleep;
    bool _periodic_stall;
    uint64_t _popped = 0;
};

static void run_slow_sink(uint64_t sleep_us, bool stall, std::chrono::seconds duration) {
    BurstMessageSource source;
    SlowSink sink(sleep_us, stall);

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&source, &sink.in),
        cler::BlockRunner(&sink)
    );

    CpuMeter cpu;
    cpu.start();
    fg.run_for(duration);
    double cpu_pct = cpu.cpu_percent();

    sink.stats.finalize();
    double secs = static_cast<double>(duration.count());
    double thr = secs > 0.0 ? sink.stats.count() / secs : 0.0;

    printf("case=slow_sink policy=fifo sink_sleep_us=%llu stall=%s items=%llu "
           "thr_ips=%.1f cpu_pct=%.0f p50_us=%.2f p99_us=%.2f max_us=%.2f drops=0\n",
           static_cast<unsigned long long>(sleep_us), stall ? "on" : "off",
           static_cast<unsigned long long>(sink.stats.count()), thr, cpu_pct,
           sink.stats.percentile(50), sink.stats.percentile(99),
           sink.stats.percentile(100));
    fflush(stdout);
}

static void case_slow_sink() {
    const std::chrono::seconds duration(3);
    for (uint64_t s : {uint64_t(0), uint64_t(100), uint64_t(1000), uint64_t(10000)}) {
        run_slow_sink(s, /*stall=*/false, duration);
    }
    // Periodic 100ms stall on top of a light 100us per-item sink.
    run_slow_sink(100, /*stall=*/true, duration);
}

// ---------------------------------------------------------------------------
// Case 4: FixedThreadPool idle CPU.
//
// This is the case that actually exercises FixedThreadPool: it needs
// blocks > num_workers, otherwise run_fixed_thread_pool silently falls back to
// ThreadPerBlock (cler.hpp:590). M independent Trickle->Drain chains (2M blocks)
// run under a small worker pool while mostly idle (sparse emit). The metric is
// cpu_pct: with the FixedThreadPool busy-spin defect, idle workers peg their
// cores (~num_workers*100); with the sweep-once fix they should back off.
//
// Default-constructible blocks with fixed-size channels so they can live in
// std::array (the flowgraph is compile-time sized).
// ---------------------------------------------------------------------------
constexpr size_t IDLE_CH = 512;

struct TrickleSource : public cler::BlockBase {
    uint64_t period_us = 10000;
    TrickleSource() : BlockBase("trickle") {}

    cler::Result<cler::Empty, cler::Error> procedure(cler::ChannelBase<float>* out) {
        auto now = steady::now();
        if (!_started) { _next = now; _started = true; }
        if (now < _next) return cler::Error::NotEnoughSamples;  // idle between emits
        if (!out->try_push(1.0f)) return cler::Error::NotEnoughSpace;
        _next += std::chrono::microseconds(period_us);
        return cler::Empty{};
    }
private:
    bool _started = false;
    steady::time_point _next{};
};

struct DrainSink : public cler::BlockBase {
    cler::Channel<float, IDLE_CH> in;
    uint64_t received = 0;
    DrainSink() : BlockBase("drain") {}

    cler::Result<cler::Empty, cler::Error> procedure() {
        float v;
        if (!in.try_pop(v)) return cler::Error::NotEnoughSamples;
        received++;
        return cler::Empty{};
    }
};

template <size_t M, size_t... I>
static void run_ftp_idle_impl(size_t num_workers, uint64_t period_us,
                              std::chrono::seconds duration,
                              std::index_sequence<I...>) {
    std::array<TrickleSource, M> srcs;
    std::array<DrainSink, M> sinks;
    for (size_t i = 0; i < M; ++i) srcs[i].period_us = period_us;

    cler::FlowGraphConfig cfg;
    cfg.scheduler = cler::SchedulerType::FixedThreadPool;
    cfg.num_workers = num_workers;  // < 2M, so FixedThreadPool actually engages

    auto fg = cler::make_desktop_flowgraph(
        cler::BlockRunner(&srcs[I], &sinks[I].in)...,
        cler::BlockRunner(&sinks[I])...
    );

    CpuMeter cpu;
    cpu.start();
    fg.run_for(duration, cfg);
    double cpu_pct = cpu.cpu_percent();

    uint64_t total = 0;
    for (auto& s : sinks) total += s.received;

    printf("case=ftp_idle blocks=%zu workers=%zu period_us=%llu items=%llu cpu_pct=%.0f\n",
           2 * M, num_workers, static_cast<unsigned long long>(period_us),
           static_cast<unsigned long long>(total), cpu_pct);
    fflush(stdout);
}

template <size_t M>
static void run_ftp_idle(size_t num_workers, uint64_t period_us,
                         std::chrono::seconds duration) {
    run_ftp_idle_impl<M>(num_workers, period_us, duration, std::make_index_sequence<M>{});
}

static void case_ftp_idle() {
    const std::chrono::seconds duration(3);
    // 16 chains = 32 blocks, 2 workers -> FixedThreadPool engages (32 > 2).
    for (uint64_t p : {uint64_t(1000), uint64_t(10000), uint64_t(100000)}) {
        run_ftp_idle<16>(/*num_workers=*/2, p, duration);
    }
}

int main(int argc, char** argv) {
    std::string which = (argc > 1) ? argv[1] : "all";

    if (which == "low_rate" || which == "all") {
        case_low_rate();
    }
    if (which == "contended" || which == "all") {
        case_contended();
    }
    if (which == "slow_sink" || which == "all") {
        case_slow_sink();
    }
    if (which == "ftp_idle" || which == "all") {
        case_ftp_idle();
    }

    return 0;
}
